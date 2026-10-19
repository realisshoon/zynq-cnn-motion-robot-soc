/* ABI 1: processed relative XYZ + stored BodyFrame; never raw sensor XYZ.
 * No reconstruction, branch selection, 2D/XYZ filtering, Agent2 or HAL calls.
 * Source flags are provenance. Solver flags below are independently computed. */
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "forearm_mapping_internal.h"
#define API __declspec(dllexport)
typedef struct {
    ForearmMappingContext fm;
    HumanForearmTarget last;
    unsigned epoch, frame;
    double time, major_age;
    int started, has_major, has_wrist, seeded;
    float held_grip;
} Xyz;
/* status: 0 WAITING_VALID, 1 FRESH, 2 WRIST_HOLD, 3 INITIAL_HELD_SEED,
 * 4 MAJOR_HOLD, 5 MAJOR_INVALID, 6 HAND_FAILED, 7 WRIST_UNAVAILABLE.
 * info[0..6]: solver target_valid, wrist_valid, hand_fresh, observable,
 * status, geometry major calculated, computed target frame ID (uint bits).
 * Source flags never copied as success. */
API int xyz_abi_version(void){return 1;}
API void *xyz_create(unsigned epoch,float applied_grip){
    Xyz *s;if(!isfinite(applied_grip)||applied_grip<0||applied_grip>1)return NULL;
    s=calloc(1,sizeof(*s));if(!s)return NULL;
    forearm_mapping_init(&s->fm);s->epoch=epoch;s->held_grip=applied_grip;return s;
}
API void xyz_destroy(void *s){free(s);}
static Point3D point(const float *p){return (Point3D){p[0],p[1],p[2],0};}
static int basis_ok(const float *b){
    Point3D x=point(b),y=point(b+3),z=point(b+6);float tol=.002f;
    return fabsf(pm_vlen(x)-1)<tol&&fabsf(pm_vlen(y)-1)<tol&&fabsf(pm_vlen(z)-1)<tol&&
        fabsf(pm_vdot(x,y))<tol&&fabsf(pm_vdot(y,z))<tol&&fabsf(pm_vdot(z,x))<tol&&
        pm_vdot(pm_vcross(x,y),z)>1-tol;
}
static void reset_hand(Xyz *s){
    PoseMappingContext *p=&s->fm.pose;
    s->has_wrist=0;p->hand_angle_valid=0;p->prev_hand_normal_valid=0;
    p->prev_roll_raw_valid=0;p->prev_pitch_raw_valid=0;
}
/* xyz: SL,SR,E,W,thumb,index. basis: X,Y,Z in camera coordinates.
 * flags: target,major,finger,observable,hand,wrist,gripper_hold,body,
 *        branch_selected,branch_ever_selected. No per-point sensor quality.
 * compatibility_span is guard-only with update_gripper=0. Never measured px.
 * mode: 0 CSV auxiliary gripper, 1 entry Applied gripper HOLD.
 * Negative rc rejects the row WITHOUT committing history. */
API int xyz_step(void *h,unsigned epoch,unsigned frame,double time,
 const float *xyz,const float *basis,const int *flags,float auxiliary_grip,
 int mode,float compatibility_span,float *human,int *info){
    Xyz *s=h,work;HumanForearmTarget fresh={0};int i,major,hand=0,status=0;
    double dt;float filter_dt;
    if(!s||!xyz||!basis||!flags||!human||!info)return -1;
    if(epoch!=s->epoch)return -2;
    if(!isfinite(time)||time<0||(s->started&&(time<=s->time||frame==s->frame)))return -3;
    for(i=0;i<18;i++)if(!isfinite(xyz[i]))return -4;
    for(i=0;i<9;i++)if(!isfinite(basis[i]))return -4;
    for(i=0;i<10;i++)if(flags[i]!=0&&flags[i]!=1)return -4;
    if((mode!=0&&mode!=1)||!isfinite(compatibility_span)||compatibility_span<=PM_EPS)return -4;
    if(mode==0&&(!isfinite(auxiliary_grip)||auxiliary_grip<0||auxiliary_grip>1))return -4;
    if(flags[7]&&!basis_ok(basis))return -5;
    work=*s;dt=s->started?time-s->time:.05;filter_dt=pm_sanitize_filter_dt((float)dt);
    work.started=1;work.time=time;work.frame=frame;work.major_age+=dt;
    work.fm.pose.shoulder_l_3d=point(xyz);work.fm.pose.shoulder_r_3d=point(xyz+3);
    work.fm.pose.elbow_3d=point(xyz+6);work.fm.pose.wrist_3d=point(xyz+9);
    work.fm.pose.finger1_3d=point(xyz+12);work.fm.pose.finger2_3d=point(xyz+15);
    work.fm.pose.body_x_axis=point(basis);work.fm.pose.body_y_axis=point(basis+3);
    work.fm.pose.body_z_axis=point(basis+6);work.fm.pose.body_frame_valid=(unsigned char)flags[7];
    major=flags[1]&&flags[7]&&fm_calculate_angles(&work.fm,filter_dt,&fresh)==0;
    if(major){
        work.major_age=0;work.has_major=1;
        fresh.wrist_pitch_deg=work.last.wrist_pitch_deg;fresh.wrist_roll_deg=work.last.wrist_roll_deg;
        /* Source wrist invalid cannot authorize a new hand solution. A one-time
         * historical branch seed is labelled held, never a fresh observation. */
        int initial=!work.seeded&&!work.has_wrist&&flags[0]&&flags[5]&&flags[8]&&flags[9]&&!flags[4];
        int observe=flags[0]&&flags[4]&&flags[2]&&flags[5];
        if((initial||observe)&&work.fm.elbow_roll_initialized){
            PoseMappingContext trial=work.fm.pose;HumanJointTarget target={0};
            if(pm_calculate_hand_with_reference_ex(&trial,compatibility_span,(float)dt,filter_dt,
                &work.fm.wrist_reference,0,&target)==0){
                work.fm.pose=trial;work.has_wrist=1;work.seeded=1;hand=observe;
                fresh.wrist_pitch_deg=target.wrist_pitch_deg;fresh.wrist_roll_deg=target.wrist_roll_deg;
                status=initial?3:1;
            }else status=6;
        }else status=work.has_wrist?2:7;
        fresh.valid=(unsigned char)flags[0];
        fresh.wrist_valid=(unsigned char)(work.has_wrist&&flags[5]);
        fresh.hand_fresh=(unsigned char)hand;fresh.frame_id=frame;
        work.last=fresh;
    }else{
        fresh=work.last;fresh.hand_fresh=0;fresh.elbow_roll_observable=0;
        fresh.valid=(unsigned char)(flags[0]&&work.has_major&&work.major_age<=PM_TARGET_HOLD_SEC);
        fresh.wrist_valid=(unsigned char)(fresh.valid&&work.has_wrist&&flags[5]);
        status=fresh.valid?4:5;
        if(work.major_age>PM_TARGET_HOLD_SEC){reset_hand(&work);work.has_major=0;fresh.wrist_valid=0;}
    }
    if(!flags[0]){fresh.valid=0;status=0;fresh.hand_fresh=0;}
    fresh.gripper_norm=mode==0?auxiliary_grip:work.held_grip;
    human[0]=fresh.elbow_roll_deg;human[1]=fresh.elbow_pitch_deg;human[2]=fresh.wrist_pitch_deg;
    human[3]=fresh.wrist_roll_deg;human[4]=fresh.gripper_norm;
    info[0]=fresh.valid;info[1]=fresh.wrist_valid;info[2]=fresh.hand_fresh;
    info[3]=fresh.elbow_roll_observable;info[4]=status;info[5]=major;
    info[6]=(int)fresh.frame_id;
    *s=work;return 0;
}
