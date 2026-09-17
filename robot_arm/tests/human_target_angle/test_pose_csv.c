#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "human_target_angle/pose_mapping.h"

/*
 * Python이 만든 real_person_pose2d.csv를 읽어서
 * 실제 Agent1 pose_mapping_update()에 입력하는 테스트.
 *
 * 입력 CSV:
 * frame_id,time_sec,frame_valid,...
 *
 * 출력 CSV:
 * Agent1 내부 3D 좌표 + 최종 HumanJointTarget
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

    FILE *in = fopen(in_name, "r");
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
    HumanPose2D pose;
    HumanJointTarget target;

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

    if (pose_mapping_init(&ctx) != 0) {
        fclose(in);
        fclose(out);
        return 1;
    }

    fprintf(out,
        "frame_id,time_sec,target_valid,"
        "shoulder_l_x3d,shoulder_l_y3d,shoulder_l_z,"
        "shoulder_r_x3d,shoulder_r_y3d,shoulder_r_z,"
        "elbow_x3d,elbow_y3d,elbow_z,"
        "wrist_x3d,wrist_y3d,wrist_z,"
        "finger1_x3d,finger1_y3d,finger1_z,"
        "finger2_x3d,finger2_y3d,finger2_z,"
        "base_deg,shoulder_deg,elbow_deg,wrist_pitch_deg,wrist_roll_deg,gripper_norm\n"
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

        float dt = first ? (1.0f / 15.0f) : (time_sec - prev_t);
        if (dt <= 0.0f) dt = 1.0f / 15.0f;
        prev_t = time_sec;
        first = 0;

        (void)pose_mapping_update(
            &ctx, &pose, POSE_ARM_RIGHT, dt, &target
        );

        fprintf(out,
            "%u,%.6f,%u,"
            "%.6f,%.6f,%.6f,"
            "%.6f,%.6f,%.6f,"
            "%.6f,%.6f,%.6f,"
            "%.6f,%.6f,%.6f,"
            "%.6f,%.6f,%.6f,"
            "%.6f,%.6f,%.6f,"
            "%.6f,%.6f,%.6f,%.6f,%.6f,%.1f\n",
            frame_id,time_sec,(unsigned)target.valid,
            ctx.shoulder_l_3d.x,ctx.shoulder_l_3d.y,ctx.shoulder_l_3d.z,
            ctx.shoulder_r_3d.x,ctx.shoulder_r_3d.y,ctx.shoulder_r_3d.z,
            ctx.elbow_3d.x,ctx.elbow_3d.y,ctx.elbow_3d.z,
            ctx.wrist_3d.x,ctx.wrist_3d.y,ctx.wrist_3d.z,
            ctx.finger1_3d.x,ctx.finger1_3d.y,ctx.finger1_3d.z,
            ctx.finger2_3d.x,ctx.finger2_3d.y,ctx.finger2_3d.z,
            target.base_deg,target.shoulder_deg,target.elbow_deg,
            target.wrist_pitch_deg,target.wrist_roll_deg,target.gripper_norm
        );
    }

    fclose(in);
    fclose(out);
    printf("[OK] %s\n", out_name);
    return 0;
}
