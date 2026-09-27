#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "human_target_angle/forearm_mapping.h"

/*
 * Python이 만든 real_person_pose2d.csv를 읽어서
 * 실제 Agent1 forearm_mapping_update()에 입력하는 테스트.
 *
 * 입력 CSV:
 * frame_id,time_sec,frame_valid,...
 *
 * 출력 CSV:
 * Agent1 내부 3D 좌표 + HumanForearmTarget (legacy angle columns removed)
 */

static Point2D make_p2(float x, float y, int valid)
{
    Point2D p;
    p.x = x;
    p.y = y;
    p.valid = valid ? 1U : 0U;
    return p;
}

int main(int argc, char **argv)
{
    const char *in_name  = (argc >= 2) ? argv[1] : "real_person_pose2d.csv";
    const char *out_name = (argc >= 3) ? argv[2] : "agent1_real_result.csv";

    FILE *in;
    FILE *out = NULL;
    char line[2048];
    uint32_t frame_id;
    float time_sec;
    int frame_valid;
    float slx,sly,srx,sry,ex,ey,wx,wy,f1x,f1y,f2x,f2y;
    int slv,srv,ev,wv,f1v,f2v;
    float prev_t = 0.0f;
    int first = 1;
    PoseMappingContext ctx;
    ForearmMappingContext mapping;
    HumanPose2D pose;
    HumanForearmTarget target;
    int update_ret;
    int rows = 0;
    PoseArmSide side = POSE_ARM_RIGHT;

    if (argc < 3 || argc > 4 || (argc == 4 && strcmp(argv[3], "left") != 0 && strcmp(argv[3], "right") != 0)) {
        fprintf(stderr, "Usage: %s input.csv output.csv [right|left]\n"
                "Human BodyFrame angles; default arm=right.\n", argv[0]);
        return 1;
    }
    if (argc == 4 && strcmp(argv[3], "left") == 0) side = POSE_ARM_LEFT;
    if (strcmp(in_name, out_name) == 0) return 1;
    forearm_mapping_init(&mapping);
    in = fopen(in_name, "r");

    if (!in) {
        fprintf(stderr, "input open failed: %s\n", in_name);
        return 1;
    }
    out = fopen(out_name, "w");
    if (!out) {
        fclose(in);
        fprintf(stderr, "output open failed: %s\n", out_name);
        return 1;
    }

    fprintf(out,
        "frame_id,time_sec,update_ret,target_valid,major_fresh,finger_fresh,"
        "shoulder_l_x3d,shoulder_l_y3d,shoulder_l_z,"
        "shoulder_r_x3d,shoulder_r_y3d,shoulder_r_z,"
        "elbow_x3d,elbow_y3d,elbow_z,"
        "wrist_x3d,wrist_y3d,wrist_z,"
        "finger1_x3d,finger1_y3d,finger1_z,"
        "finger2_x3d,finger2_y3d,finger2_z,"
        "elbow_roll_deg,elbow_pitch_deg,wrist_pitch_deg,wrist_roll_deg,gripper_norm,"
        "target_frame_id,elbow_roll_observable,hand_fresh,body_frame_valid,raw_elbow_roll_deg,raw_elbow_pitch_deg,"
        "body_x_x,body_x_y,body_x_z,body_y_x,body_y_y,body_y_z,body_z_x,body_z_y,body_z_z,active_arm\n"
    );

    /* header skip */
    if (!fgets(line, sizeof(line), in)) {
        fclose(in); fclose(out); return 1;
    }

    while (fgets(line, sizeof(line), in)) {
        int n = sscanf(
            line,
            "%u,%f,%d,"
            "%f,%f,%d,"
            "%f,%f,%d,"
            "%f,%f,%d,"
            "%f,%f,%d,"
            "%f,%f,%d,"
            "%f,%f,%d",
            &frame_id,&time_sec,&frame_valid,
            &slx,&sly,&slv,
            &srx,&sry,&srv,
            &ex,&ey,&ev,
            &wx,&wy,&wv,
            &f1x,&f1y,&f1v,
            &f2x,&f2y,&f2v
        );
        if (n != 21) {
            fprintf(stderr, "CSV parse skip: %s", line);
            continue;
        }

        memset(&pose, 0, sizeof(pose));
        pose.shoulder_l = make_p2(slx,sly,slv);
        pose.shoulder_r = make_p2(srx,sry,srv);
        pose.elbow = make_p2(ex,ey,ev);
        pose.wrist = make_p2(wx,wy,wv);
        pose.finger1 = make_p2(f1x,f1y,f1v);
        pose.finger2 = make_p2(f2x,f2y,f2v);
        pose.frame_id = frame_id;
        pose.valid = frame_valid ? 1U : 0U;

        float dt = first ? (1.0f / 20.0f) : (time_sec - prev_t);
        if (dt <= 0.0f) dt = 1.0f / 20.0f;
        prev_t = time_sec;
        first = 0;

        update_ret = forearm_mapping_update(
            &mapping, &pose, side, dt, &target
        );
        ctx = mapping.pose;
        rows++;

        fprintf(out,
            "%u,%.6f,%d,%u,%u,%u,"
            "%.6f,%.6f,%.6f,"
            "%.6f,%.6f,%.6f,"
            "%.6f,%.6f,%.6f,"
            "%.6f,%.6f,%.6f,"
            "%.6f,%.6f,%.6f,"
            "%.6f,%.6f,%.6f,"
            "%.6f,%.6f,%.6f,%.6f,%.1f,"
            "%u,%u,%u,%u,%.6f,%.6f,"
            "%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%u\n",
            frame_id,time_sec,update_ret,(unsigned)target.valid,
            (unsigned)(ctx.shoulder_l.fresh && ctx.shoulder_r.fresh &&
                       ctx.elbow.fresh && ctx.wrist.fresh),
            (unsigned)(ctx.finger1.fresh && ctx.finger2.fresh),
            ctx.shoulder_l_3d.x,ctx.shoulder_l_3d.y,ctx.shoulder_l_3d.z,
            ctx.shoulder_r_3d.x,ctx.shoulder_r_3d.y,ctx.shoulder_r_3d.z,
            ctx.elbow_3d.x,ctx.elbow_3d.y,ctx.elbow_3d.z,
            ctx.wrist_3d.x,ctx.wrist_3d.y,ctx.wrist_3d.z,
            ctx.finger1_3d.x,ctx.finger1_3d.y,ctx.finger1_3d.z,
            ctx.finger2_3d.x,ctx.finger2_3d.y,ctx.finger2_3d.z,
            target.elbow_roll_deg,target.elbow_pitch_deg,
            target.wrist_pitch_deg,target.wrist_roll_deg,target.gripper_norm,
            target.frame_id,target.elbow_roll_observable,target.hand_fresh,ctx.body_frame_valid,
            mapping.raw_elbow_roll_deg,mapping.raw_elbow_pitch_deg,
            ctx.body_x_axis.x,ctx.body_x_axis.y,ctx.body_x_axis.z,
            ctx.body_y_axis.x,ctx.body_y_axis.y,ctx.body_y_axis.z,
            ctx.body_z_axis.x,ctx.body_z_axis.y,ctx.body_z_axis.z,(unsigned)side
        );
    }

    fclose(in);
    fclose(out);
    printf("[OK] %s (%d rows)\n", out_name, rows);
    return rows > 0 ? 0 : 1;
}
