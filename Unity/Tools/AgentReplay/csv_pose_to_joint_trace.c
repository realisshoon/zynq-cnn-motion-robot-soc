#include <errno.h>
#include <ctype.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "integration/agent_pipeline.h"
#include "output_controller/servo_hal.h"
#include "drivers/servo_pwm_driver.h"

#define DEFAULT_TICK_SEC 0.020
#define DEFAULT_FIRST_DT_SEC 0.050f
#define DEFAULT_SETTLE_MS 1000U
#define LINE_CAP 4096

typedef struct {
    double time_sec;
    HumanPose2D pose;
} PoseRow;

typedef struct {
    PoseRow *items;
    size_t count;
    size_t capacity;
} PoseRows;

static void usage(const char *exe)
{
    fprintf(stderr,
            "Usage: %s <input_pose.csv> <output_joint_trace.csv> [--settle-ms N]\n"
            "\n"
            "Input CSV columns:\n"
            "  frame_id,time_sec,frame_valid,shoulder_l_x,shoulder_l_y,shoulder_l_valid,\n"
            "  shoulder_r_x,shoulder_r_y,shoulder_r_valid,elbow_x,elbow_y,elbow_valid,\n"
            "  wrist_x,wrist_y,wrist_valid,finger1_x,finger1_y,finger1_valid,\n"
            "  finger2_x,finger2_y,finger2_valid\n",
            exe);
}

static int rows_push(PoseRows *rows, const PoseRow *row)
{
    if (rows->count == rows->capacity) {
        size_t next = rows->capacity ? rows->capacity * 2U : 512U;
        PoseRow *tmp = (PoseRow *)realloc(rows->items, next * sizeof(*tmp));
        if (tmp == NULL) return 0;
        rows->items = tmp;
        rows->capacity = next;
    }
    rows->items[rows->count++] = *row;
    return 1;
}

static int finite_pixel(float v, float max_exclusive)
{
    return isfinite(v) && v >= 0.0f && v < max_exclusive;
}

static void sanitize_point(Point2D *p, unsigned frame_valid, unsigned point_valid,
                           float x, float y)
{
    p->x = x;
    p->y = y;
    p->valid = (uint8_t)(frame_valid && point_valid &&
                         finite_pixel(x, 1280.0f) && finite_pixel(y, 720.0f));
    if (!p->valid) {
        p->x = 0.0f;
        p->y = 0.0f;
    }
}

static int parse_pose_line(const char *line, PoseRow *out)
{
    unsigned long long frame_id;
    unsigned frame_valid;
    unsigned sl_valid, sr_valid, elbow_valid, wrist_valid, f1_valid, f2_valid;
    float sl_x, sl_y, sr_x, sr_y, elbow_x, elbow_y, wrist_x, wrist_y;
    float f1_x, f1_y, f2_x, f2_y;
    double time_sec;
    int n, consumed = 0;

    memset(out, 0, sizeof(*out));

    n = sscanf(line,
               "%llu,%lf,%u,%f,%f,%u,%f,%f,%u,%f,%f,%u,%f,%f,%u,%f,%f,%u,%f,%f,%u%n",
               &frame_id, &time_sec, &frame_valid,
               &sl_x, &sl_y, &sl_valid,
               &sr_x, &sr_y, &sr_valid,
               &elbow_x, &elbow_y, &elbow_valid,
               &wrist_x, &wrist_y, &wrist_valid,
               &f1_x, &f1_y, &f1_valid,
               &f2_x, &f2_y, &f2_valid, &consumed);

    if (n != 21) return 0;
    while (isspace((unsigned char)line[consumed])) ++consumed;
    if (line[consumed] != '\0' || frame_id > UINT32_MAX) return 0;
    if (!isfinite(time_sec) || time_sec < 0.0) return 0;
    if (frame_valid > 1U || sl_valid > 1U || sr_valid > 1U ||
        elbow_valid > 1U || wrist_valid > 1U || f1_valid > 1U || f2_valid > 1U) {
        return 0;
    }

    out->time_sec = time_sec;
    out->pose.frame_id = (uint32_t)frame_id;
    out->pose.valid = (uint8_t)frame_valid;
    sanitize_point(&out->pose.shoulder_l, frame_valid, sl_valid, sl_x, sl_y);
    sanitize_point(&out->pose.shoulder_r, frame_valid, sr_valid, sr_x, sr_y);
    sanitize_point(&out->pose.elbow, frame_valid, elbow_valid, elbow_x, elbow_y);
    sanitize_point(&out->pose.wrist, frame_valid, wrist_valid, wrist_x, wrist_y);
    sanitize_point(&out->pose.finger1, frame_valid, f1_valid, f1_x, f1_y);
    sanitize_point(&out->pose.finger2, frame_valid, f2_valid, f2_x, f2_y);
    return 1;
}

static int load_pose_csv(const char *path, PoseRows *rows)
{
    FILE *fp = fopen(path, "rb");
    char line[LINE_CAP];
    unsigned line_no = 0;
    double previous_time = -1.0;

    if (fp == NULL) {
        fprintf(stderr, "ERROR: cannot open input: %s (%s)\n", path, strerror(errno));
        return 0;
    }

    while (fgets(line, sizeof(line), fp) != NULL) {
        PoseRow row;
        ++line_no;

        if (line_no == 1U && strstr(line, "frame_id") != NULL) continue;
        if (line[0] == '\0' || line[0] == '\n' || line[0] == '\r' || line[0] == '#') continue;

        if (!parse_pose_line(line, &row)) {
            fprintf(stderr, "ERROR: bad CSV row at line %u\n", line_no);
            fclose(fp);
            return 0;
        }
        if (previous_time > row.time_sec + 1e-9) {
            fprintf(stderr, "ERROR: time_sec goes backwards at line %u\n", line_no);
            fclose(fp);
            return 0;
        }
        previous_time = row.time_sec;

        if (!rows_push(rows, &row)) {
            fprintf(stderr, "ERROR: out of memory\n");
            fclose(fp);
            return 0;
        }
    }

    fclose(fp);
    if (rows->count == 0U) {
        fprintf(stderr, "ERROR: no pose rows found\n");
        return 0;
    }
    return 1;
}

static int write_trace_row(FILE *out, uint32_t frame_id, uint32_t source_frame_id,
                           double time_sec, const JointCommand *cmd)
{
    return fprintf(out,
                   "%.6f,%u,%u,%u,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g\n",
                   time_sec,
                   frame_id,
                   source_frame_id,
                   cmd->valid ? 1U : 0U,
                   cmd->base_deg,
                   cmd->shoulder_deg,
                   cmd->elbow_deg,
                   cmd->wrist_pitch_deg,
                   cmd->wrist_roll_deg,
                   cmd->gripper_norm) > 0;
}

/* 같은 ctx->output을 실제 Agent3와 Unity trace에 전달한다.
 * canonical driver의 host mock은 128-write log만 가지므로 tick마다 mock register/log를 초기화한다.
 * Agent1/Agent2/context는 초기화하지 않는다. 실제 보드에는 이 host runner를 배포하지 않는다. */
static int physical_output(FILE *fp, AgentPipelineContext *ctx, uint32_t tick, uint32_t source, double time_sec)
{
    JointCommand shared = ctx->output;
    ServoPwmDriverMockWrite entry;
    uint16_t values[6];
    unsigned i;
    servo_hal_init();
    if (!servo_hal_enable() || !agent3_run(ctx)) return 0;
    values[0]=ctx->pwm.base_pwm_us; values[1]=ctx->pwm.shoulder_pwm_us;
    values[2]=ctx->pwm.elbow_pwm_us; values[3]=ctx->pwm.wrist_pitch_pwm_us;
    values[4]=ctx->pwm.wrist_roll_pwm_us; values[5]=ctx->pwm.gripper_pwm_us;
    if (memcmp(&shared,&ctx->output,sizeof(shared)) != 0 || servo_pwm_driver_mock_get_log_count()!=8) return 0;
    if (!servo_pwm_driver_mock_get_log(0,&entry) || entry.offset!=0x18 || entry.value!=1) return 0;
    for(i=0;i<6;i++) {
        if(values[i]<500 || values[i]>2500 || !servo_pwm_driver_mock_get_log(i+1,&entry) || entry.offset!=i*4 || entry.value!=values[i]) return 0;
    }
    if(!servo_pwm_driver_mock_get_log(7,&entry) || entry.offset!=0x1c || entry.value!=1) return 0;
    return fprintf(fp,"%.6f,%u,%u,%u,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%u,%u,%u,%u,%u,%u\n",
        time_sec,tick,source,shared.valid,shared.base_deg,shared.shoulder_deg,shared.elbow_deg,
        shared.wrist_pitch_deg,shared.wrist_roll_deg,shared.gripper_norm,
        values[0],values[1],values[2],values[3],values[4],values[5])>0;
}

int main(int argc, char **argv)
{
    const char *input_path;
    const char *output_path;
    unsigned settle_ms = DEFAULT_SETTLE_MS;
    PoseRows rows = {0};
    AgentPipelineContext pipeline;
    FILE *out = NULL;
    FILE *physical = NULL, *frames = NULL, *summary = NULL;
    size_t next_pose = 0U;
    double next_tick_sec = DEFAULT_TICK_SEC;
    double last_pose_time = 0.0;
    double end_time;
    double previous_pose_time = -1.0;
    uint32_t latest_source_frame = 0U;
    uint32_t output_frame_id = 1U;
    unsigned accepted_pose_rows = 0U;
    unsigned rejected_pose_rows = 0U;
    unsigned frame_valid_rows=0, a1_new=0, a1_hold=0, a1_invalid=0;
    unsigned a2_results[5]={0};

    if (argc < 3) {
        usage(argv[0]);
        return 2;
    }
    input_path = argv[1];
    output_path = argv[2];

    if (argc == 5 && strcmp(argv[3], "--settle-ms") == 0) {
        char *end = NULL;
        unsigned long value = strtoul(argv[4], &end, 10);
        if (end == argv[4] || *end != '\0' || value > 60000UL) {
            fprintf(stderr, "ERROR: invalid --settle-ms\n");
            return 2;
        }
        settle_ms = (unsigned)value;
    } else if (argc != 3) {
        usage(argv[0]);
        return 2;
    }

    if (!load_pose_csv(input_path, &rows)) {
        free(rows.items);
        return 1;
    }

    servo_hal_init();
    if (agent_pipeline_init(&pipeline) != 0) {
        fprintf(stderr, "ERROR: agent_pipeline_init failed\n");
        free(rows.items);
        return 1;
    }

    out = fopen(output_path, "wb");
    if (out == NULL) {
        fprintf(stderr, "ERROR: cannot open output: %s (%s)\n", output_path, strerror(errno));
        free(rows.items);
        return 1;
    }

    fprintf(out,
            "time_sec,frame_id,source_frame_id,valid,base_deg,shoulder_deg,elbow_deg,wrist_pitch_deg,wrist_roll_deg,gripper_norm\n");
    physical=fopen("physical_command_pwm_trace.csv","wb");
    frames=fopen("pose_pipeline_trace.csv","wb");
    if(!physical || !frames) {fprintf(stderr,"ERROR: cannot open audit traces\n");return 1;}
    fprintf(physical,"time_sec,frame_id,source_frame_id,valid,base_deg,shoulder_deg,elbow_deg,wrist_pitch_deg,wrist_roll_deg,gripper_norm,base_pwm_us,shoulder_pwm_us,elbow_pwm_us,wrist_pitch_pwm_us,wrist_roll_pwm_us,gripper_pwm_us\n");
    fprintf(frames,"source_frame_id,time_sec,frame_valid,body_valid,fingers_valid,a1_rc,a1_target_valid,a2_result\n");

    /* Unity replay starts from the same home pose used by the real Agent pipeline. */
    if (!physical_output(physical,&pipeline,output_frame_id,0U,0.0) || !write_trace_row(out, output_frame_id++, 0U, 0.0, &pipeline.output)) {
        fprintf(stderr, "ERROR: output write failed\n");
        fclose(out);
        free(rows.items);
        return 1;
    }

    last_pose_time = rows.items[rows.count - 1U].time_sec;
    end_time = last_pose_time + (double)settle_ms / 1000.0;

    while (next_tick_sec <= end_time + 1e-9) {
        while (next_pose < rows.count && rows.items[next_pose].time_sec <= next_tick_sec + 1e-9) {
            PoseRow *row = &rows.items[next_pose];
            float dt_sec = previous_pose_time < 0.0
                ? DEFAULT_FIRST_DT_SEC
                : (float)(row->time_sec - previous_pose_time);

            if (!(dt_sec > 0.0f) || !isfinite(dt_sec)) dt_sec = DEFAULT_FIRST_DT_SEC;

            (void)agent1_run(&pipeline, &row->pose, dt_sec);
            if (agent2_run(&pipeline)) ++accepted_pose_rows;
            else ++rejected_pose_rows;
            frame_valid_rows += row->pose.valid ? 1U:0U;
            if(pipeline.a1_rc==1) ++a1_new;
            else if(pipeline.a1_rc==0) ++a1_hold;
            else ++a1_invalid;
            if(pipeline.a2_result>4) {fprintf(stderr,"ERROR: unexpected a2_result\n");return 1;}
            ++a2_results[pipeline.a2_result];
            fprintf(frames,"%u,%.6f,%u,%u,%u,%d,%u,%u\n",row->pose.frame_id,row->time_sec,row->pose.valid,
                row->pose.shoulder_l.valid && row->pose.shoulder_r.valid && row->pose.elbow.valid && row->pose.wrist.valid,
                row->pose.finger1.valid && row->pose.finger2.valid,pipeline.a1_rc,pipeline.target_ready,pipeline.a2_result);

            latest_source_frame = row->pose.frame_id;
            previous_pose_time = row->time_sec;
            ++next_pose;
        }

        if (!agent2_tick(&pipeline)) {
            fprintf(stderr, "ERROR: Agent2 output invalid at t=%.3f\n", next_tick_sec);
            fclose(out);
            free(rows.items);
            return 1;
        }

        if (!physical_output(physical,&pipeline,output_frame_id,latest_source_frame,next_tick_sec) ||
            !write_trace_row(out, output_frame_id++, latest_source_frame,
                             next_tick_sec, &pipeline.output)) {
            fprintf(stderr, "ERROR: output write failed\n");
            fclose(out);
            free(rows.items);
            return 1;
        }

        next_tick_sec += DEFAULT_TICK_SEC;
    }

    fclose(out);
    fclose(physical);fclose(frames);
    summary=fopen("replay_summary.json","wb");
    if(!summary) return 1;
    fprintf(summary,"{\n  \"csv_rows\":%zu,\n  \"frame_valid\":%u,\n  \"frame_invalid\":%zu,\n  \"a1_new\":%u,\n  \"a1_hold\":%u,\n  \"a1_invalid\":%u,\n  \"a2_none\":%u,\n  \"a2_new\":%u,\n  \"a2_same\":%u,\n  \"a2_reject_validate\":%u,\n  \"a2_reject_safety\":%u,\n  \"control_ticks\":%u,\n  \"trace_rows_including_home\":%u,\n  \"physical_outputs\":%u,\n  \"servo_errors\":%u,\n  \"hardware\":\"HOST_MOCK_ONLY\"\n}\n",
        rows.count,frame_valid_rows,rows.count-frame_valid_rows,a1_new,a1_hold,a1_invalid,
        a2_results[0],a2_results[1],a2_results[2],a2_results[3],a2_results[4],pipeline.ticks,output_frame_id-1,pipeline.servo_writes,pipeline.servo_errors);
    fclose(summary);
    free(rows.items);

    printf("PASS: pose rows=%zu, accepted/held=%u, rejected/no-target=%u\n",
           rows.count, accepted_pose_rows, rejected_pose_rows);
    printf("PASS: output=%s, control_tick=20ms, settle=%ums\n", output_path, settle_ms);
    printf("NOTE: trace frame_id is a 50Hz Unity transport sequence; source_frame_id preserves the pose/CNN frame id.\n");
    return 0;
}
