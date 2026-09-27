/*
 * keypoint_overlay.c
 *
 *  Created on: 2026. 9. 22.
 *      Author: kccistc
 */

#include "keypoint_overlay.h"

#include "xparameters.h"
#include "xil_io.h"
#include "xil_printf.h"
#include "sleep.h"

/* -------------------------------------------------------------------------
 * Hardware base address
 * ------------------------------------------------------------------------- */
#define KPO_BASE XPAR_KEYPOINT_OVERLAY_0_BASEADDR

/* -------------------------------------------------------------------------
 * Register map
 * ------------------------------------------------------------------------- */
#define KPO_CTRL            0x00u
#define KPO_STATUS          0x04u
#define KPO_VALID_FLAGS     0x08u
#define KPO_MARKER_COLOR    0x0Cu
#define KPO_MARKER_RADIUS   0x10u
#define KPO_SOURCE_FRAME_ID 0x14u
#define KPO_COMMIT          0x18u

#define KPO_JOINT0          0x20u
#define KPO_JOINT_STRIDE    0x04u
#define KPO_RED_MARKER      0x64u
#define KPO_BLUE_MARKER     0x68u
#define KPO_BODY_COLOR      0x6Cu
#define KPO_ARM_COLOR       0x70u
#define KPO_GREEN_MARKER    0x74u

#define KPO_STATUS_PENDING  0x00000001u
#define KPO_CTRL_ENABLE       0x00000001u
#define KPO_CTRL_JOINT_COLOR  0x00000002u
#define KPO_CTRL_BODY_ARM_COLOR 0x00000004u

#define KPO_WR(off, val) \
    Xil_Out32((UINTPTR)(KPO_BASE + (off)), (u32)(val))

#define KPO_RD(off) \
    Xil_In32((UINTPTR)(KPO_BASE + (off)))

static int ready = 0;

static u32 kpo_sanitize_color_marker(u32 word)
{
    u32 x = word & 0x7FFu;
    u32 y = (word >> 11) & 0x3FFu;

    if ((word & 0x80000000u) == 0u || x >= 1280u || y >= 720u) {
        return 0u;
    }

    return 0x80000000u | (y << 11) | x;
}


/* -------------------------------------------------------------------------
 * Joint word
 *
 * [31:24] score
 * [23:12] y
 * [11:0]  x
 * ------------------------------------------------------------------------- */
static u32 kpo_pack_joint(u16 x, u16 y, u8 score)
{
    return (((u32)score << 24) |
            (((u32)y & 0x0FFFu) << 12) |
            ((u32)x & 0x0FFFu));
}


int kpo_init(void)
{
    if (ready) {
        return XST_SUCCESS;
    }

    /*
     * Initialize shadow registers.
     * Nothing becomes active until COMMIT is consumed at video SOF.
     */
    KPO_WR(KPO_CTRL,            0u);
    KPO_WR(KPO_VALID_FLAGS,     0u);
    KPO_WR(KPO_MARKER_COLOR,    KPO_COLOR_RED);
    KPO_WR(KPO_MARKER_RADIUS,   KPO_DEFAULT_RADIUS);
    KPO_WR(KPO_SOURCE_FRAME_ID, 0u);

    for (int i = 0; i < 17; i++) {
        KPO_WR(KPO_JOINT0 + ((u32)i * KPO_JOINT_STRIDE), 0u);
    }
    KPO_WR(KPO_RED_MARKER,   0u);
    KPO_WR(KPO_BLUE_MARKER,  0u);
    KPO_WR(KPO_GREEN_MARKER, 0u);
    KPO_WR(KPO_BODY_COLOR,   KPO_COLOR_YELLOW);
    KPO_WR(KPO_ARM_COLOR,   KPO_COLOR_CYAN);

    KPO_WR(KPO_COMMIT, 1u);

    ready = 1;

    xil_printf("keypoint_overlay: ok at 0x%08X\r\n",
               (unsigned int)KPO_BASE);

    return XST_SUCCESS;
}


void kpo_set_enable(int on)
{
    u32 ctrl;

    if (!ready) {
        return;
    }

    ctrl = KPO_RD(KPO_CTRL);

    if (on) {
        ctrl |= KPO_CTRL_ENABLE;
    }
    else {
        ctrl &= ~KPO_CTRL_ENABLE;
    }

    KPO_WR(KPO_CTRL, ctrl);
}


void kpo_set_valid_flags(u32 flags)
{
    if (!ready) {
        return;
    }

    KPO_WR(KPO_VALID_FLAGS, flags & 0x0001FFFFu);
}


void kpo_set_color(u32 color)
{
    if (!ready) {
        return;
    }

    KPO_WR(KPO_MARKER_COLOR, color & 0x00FFFFFFu);
}


void kpo_set_radius(u8 radius)
{
    if (!ready) {
        return;
    }

    KPO_WR(KPO_MARKER_RADIUS, (u32)radius);
}


void kpo_set_source_frame_id(u32 frame_id)
{
    if (!ready) {
        return;
    }

    KPO_WR(KPO_SOURCE_FRAME_ID, frame_id);
}


void kpo_set_joint(int index, u16 x, u16 y, u8 score)
{
    u32 word;

    if (!ready) {
        return;
    }

    if ((index < 0) || (index >= 17)) {
        return;
    }

    if ((x >= 1280u) || (y >= 720u)) {
        return;
    }

    word = kpo_pack_joint(x, y, score);

    KPO_WR(KPO_JOINT0 + ((u32)index * KPO_JOINT_STRIDE), word);
}


void kpo_set_body_arm_colors(u32 body_color, u32 arm_color)
{
    u32 ctrl;

    if (!ready) {
        return;
    }

    KPO_WR(KPO_BODY_COLOR, body_color & 0x00FFFFFFu);
    KPO_WR(KPO_ARM_COLOR, arm_color & 0x00FFFFFFu);

    ctrl = KPO_RD(KPO_CTRL);
    ctrl &= ~KPO_CTRL_JOINT_COLOR;
    ctrl |= KPO_CTRL_BODY_ARM_COLOR;
    KPO_WR(KPO_CTRL, ctrl);
}


void kpo_set_color_results(u32 red_marker_word, u32 blue_marker_word,
                           u32 green_marker_word)
{
    if (!ready) {
        return;
    }

    KPO_WR(KPO_RED_MARKER,   kpo_sanitize_color_marker(red_marker_word));
    KPO_WR(KPO_BLUE_MARKER,  kpo_sanitize_color_marker(blue_marker_word));
    KPO_WR(KPO_GREEN_MARKER, kpo_sanitize_color_marker(green_marker_word));
}


void kpo_commit(void)
{
    if (!ready) {
        return;
    }

    KPO_WR(KPO_COMMIT, 1u);
}


int kpo_commit_pending(void)
{
    if (!ready) {
        return 0;
    }

    return (KPO_RD(KPO_STATUS) & KPO_STATUS_PENDING) ? 1 : 0;
}


int kpo_wait_commit(u32 timeout)
{
    if (!ready) {
        return XST_FAILURE;
    }

    while (kpo_commit_pending()) {
        if (timeout == 0u) {
            return XST_FAILURE;
        }

        timeout--;
    }

    return XST_SUCCESS;
}


void kpo_clear(void)
{
    if (!ready) {
        return;
    }

    KPO_WR(KPO_VALID_FLAGS,  0u);
    KPO_WR(KPO_RED_MARKER,   0u);
    KPO_WR(KPO_BLUE_MARKER,  0u);
    KPO_WR(KPO_GREEN_MARKER, 0u);
    kpo_commit();
}


/* -------------------------------------------------------------------------
 * First hardware bring-up test
 *
 * Joint0 = center of 1280x720
 * x = 640
 * y = 360
 * radius = 3 -> 7x7 red square
 * ------------------------------------------------------------------------- */
void kpo_test_center(void)
{
    if (!ready) {
        return;
    }

    kpo_set_enable(1);
    kpo_set_color(KPO_COLOR_RED);
    kpo_set_radius(10);

    kpo_set_joint(0, 640, 360, 0);

    /* Only joint0 valid */
    kpo_set_valid_flags(0x00000001u);

    kpo_commit();

    xil_printf("keypoint_overlay: center marker requested "
               "(x=640, y=360, radius=10)\r\n");
    xil_printf("keypoint_overlay: CTRL=%08X VALID=%08X RADIUS=%08X "
               "JOINT0=%08X STATUS=%08X\r\n",
               (unsigned int)KPO_RD(KPO_CTRL),
               (unsigned int)KPO_RD(KPO_VALID_FLAGS),
               (unsigned int)KPO_RD(KPO_MARKER_RADIUS),
               (unsigned int)KPO_RD(KPO_JOINT0),
               (unsigned int)KPO_RD(KPO_STATUS));

    if (kpo_wait_commit(10000000u) == XST_SUCCESS) {
        xil_printf("keypoint_overlay: commit accepted at video SOF\r\n");
    }
    else {
        xil_printf("keypoint_overlay: COMMIT TIMEOUT - no accepted video SOF\r\n");
    }
}

void kpo_test_multi(void)
{
    if (!ready) {
        return;
    }

    kpo_set_enable(1);
    kpo_set_color(KPO_COLOR_RED);
    kpo_set_radius(3);

    /* 占쌓쏙옙트占쏙옙 5占쏙옙 占쏙옙표 */
    kpo_set_joint(0,  200, 150, 0);
    kpo_set_joint(1,  400, 250, 0);
    kpo_set_joint(2,  640, 360, 0);
    kpo_set_joint(3,  900, 500, 0);
    kpo_set_joint(4, 1100, 650, 0);

    /* Joint 0~4 valid */
    kpo_set_valid_flags(0x0000001Fu);

    kpo_commit();

    xil_printf("keypoint_overlay: multi joint test requested\r\n");
}

void kpo_set_joint_color_mode(int on)
{
    u32 ctrl;

    if (!ready) {
        return;
    }

    ctrl = KPO_RD(KPO_CTRL);

    if (on) {
        ctrl |= KPO_CTRL_JOINT_COLOR;
        ctrl &= ~KPO_CTRL_BODY_ARM_COLOR;
    }
    else {
        ctrl &= ~KPO_CTRL_JOINT_COLOR;
    }

    KPO_WR(KPO_CTRL, ctrl);
}

void kpo_test_motion(void)
{
    int x;
    int dir = 1;

    if (!ready) {
        return;
    }

    kpo_set_enable(1);
    kpo_set_color(KPO_COLOR_RED);
    kpo_set_radius(3);
    kpo_set_valid_flags(0x00000001u);

    x = 100;

    xil_printf("keypoint_overlay: motion test start\r\n");

    while (1)
    {
        /*
         * 占쏙옙占쏙옙 COMMIT占쏙옙 占쏙옙占쏙옙 SOF占쏙옙占쏙옙 占쏙옙占쏙옙占� 占쏙옙占쏙옙占쏙옙 占쏙옙占�
         */
        if (kpo_wait_commit(1000000u) != XST_SUCCESS) {
            xil_printf("keypoint_overlay: commit timeout\r\n");
            return;
        }

        /*
         * Joint0占쏙옙 화占쏙옙 占쌩억옙 占쏙옙占싱울옙占쏙옙 占승울옙 占싱듸옙
         */
        kpo_set_joint(0, (u16)x, 360, 0);

        /*
         * Shadow -> Active 占쏙옙청
         */
        kpo_commit();

        /*
         * 占쏙옙占쏙옙占쏙옙 占쏙옙占쏙옙占쏙옙占쏙옙 占쏙옙占쏙옙 占쏙옙占쏙옙 delay
         * 50 ms = 占쏙옙 20회/s 占쏙옙占쏙옙
         */
        usleep(50000);

        if (dir) {
            x += 10;

            if (x >= 1180) {
                x = 1180;
                dir = 0;
            }
        }
        else {
            x -= 10;

            if (x <= 100) {
                x = 100;
                dir = 1;
            }
        }
    }
}

void kpo_test_fake_cnn_pose(void)
{
    if (!ready) {
        return;
    }

    /*
     * Overlay ON
     * Joint占쏙옙 Color Mode ON
     */
    kpo_set_enable(1);
    kpo_set_joint_color_mode(1);

    /*
     * 占쏙옙占쏙옙占쏙옙 확占쏙옙占싹깍옙 占쏙옙占쏙옙占쏙옙 占쏙옙占쏙옙 크占쏙옙 표占쏙옙
     * radius = 5 -> 11x11 square
     */
    kpo_set_radius(5);

    /*
     * Fake CNN result
     *
     * Joint 0 : Left Shoulder
     * Joint 1 : Right Shoulder
     * Joint 2 : Left Elbow
     * Joint 3 : Right Elbow
     * Joint 4 : Left Wrist
     * Joint 5 : Right Wrist
     * Joint 6 : Left Hip
     * Joint 7 : Right Hip
     * Joint 8 : Thumb
     * Joint 9 : Index
     */

    kpo_set_joint(0, 480, 220, 0);   // Left Shoulder
    kpo_set_joint(1, 800, 220, 0);   // Right Shoulder

    kpo_set_joint(2, 420, 320, 0);   // Left Elbow
    kpo_set_joint(3, 860, 320, 0);   // Right Elbow

    kpo_set_joint(4, 360, 400, 0);   // Left Wrist
    kpo_set_joint(5, 920, 400, 0);   // Right Wrist

    kpo_set_joint(6, 540, 500, 0);   // Left Hip
    kpo_set_joint(7, 740, 500, 0);   // Right Hip

    kpo_set_joint(8, 360, 440, 0);   // Left Thumb
    kpo_set_joint(9, 360, 470, 0);   // Left Index

    kpo_set_joint(10, 920, 440, 0);   // Right Thumb
    kpo_set_joint(11, 920, 470, 0);   // Right Index

    /*
     * bit[9:0] = 1
     * Joint 0 ~ Joint 9 占쏙옙占� valid
     */
    kpo_set_valid_flags(0x00000FFFu);

    /*
     * Shadow register -> 占쏙옙占쏙옙 SOF占쏙옙占쏙옙 Active
     */
    kpo_commit();

    xil_printf("\r\n");
    xil_printf("========================================\r\n");
    xil_printf("CNN Keypoint Test\r\n");
    xil_printf("========================================\r\n");
    xil_printf("J0 : Left Shoulder  (480, 220)\r\n");
    xil_printf("J1 : Right Shoulder (800, 220)\r\n");
    xil_printf("J2 : Left Elbow     (420, 320)\r\n");
    xil_printf("J3 : Right Elbow    (860, 320)\r\n");
    xil_printf("J4 : Left Wrist     (360, 430)\r\n");
    xil_printf("J5 : Right Wrist    (920, 430)\r\n");
    xil_printf("J6 : Left Hip       (540, 500)\r\n");
    xil_printf("J7 : Right Hip      (740, 500)\r\n");
    xil_printf("J8 : Thumb          (330, 420)\r\n");
    xil_printf("J9 : Index          (310, 390)\r\n");
    xil_printf("VALID_FLAGS = 0x000003FF\r\n");
    xil_printf("Joint color mode = ON\r\n");
    xil_printf("========================================\r\n");
}


static void kpo_print_marker(const char *name, u32 word)
{
    u32 found = (word >> 31) & 1u;
    u32 x = word & 0x7FFu;
    u32 y = (word >> 11) & 0x3FFu;

    xil_printf("  %s raw=%08X found=%lu x=%lu y=%lu\r\n",
               name,
               (unsigned int)word,
               (unsigned long)found,
               (unsigned long)x,
               (unsigned long)y);
}


void kpo_debug_dump(void)
{
    u32 ctrl;
    u32 status;
    u32 valid;
    u32 red;
    u32 blue;
    u32 green;
    int i;

    if (!ready) {
        xil_printf("KPO diagnostic: driver not initialized\r\n");
        return;
    }

    ctrl = KPO_RD(KPO_CTRL);
    status = KPO_RD(KPO_STATUS);
    valid = KPO_RD(KPO_VALID_FLAGS) & 0x0001FFFFu;
    red = KPO_RD(KPO_RED_MARKER);
    blue = KPO_RD(KPO_BLUE_MARKER);
    green = KPO_RD(KPO_GREEN_MARKER);

    xil_printf("\r\nKPO diagnostic (shadow register bank)\r\n");
    xil_printf("  base/status/ctrl : %08X / %08X / %08X\r\n",
               (unsigned int)KPO_BASE,
               (unsigned int)status,
               (unsigned int)ctrl);
    xil_printf("  enable/group/per-joint/pending : %lu/%lu/%lu/%lu\r\n",
               (unsigned long)((ctrl & KPO_CTRL_ENABLE) != 0u),
               (unsigned long)((ctrl & KPO_CTRL_BODY_ARM_COLOR) != 0u),
               (unsigned long)((ctrl & KPO_CTRL_JOINT_COLOR) != 0u),
               (unsigned long)((status & KPO_STATUS_PENDING) != 0u));
    xil_printf("  valid/frame/radius: %05X / %lu / %lu\r\n",
               (unsigned int)valid,
               (unsigned long)KPO_RD(KPO_SOURCE_FRAME_ID),
               (unsigned long)(KPO_RD(KPO_MARKER_RADIUS) & 0xFFu));
    xil_printf("  body color (R-B-G): %06X  expected yellow=%06X\r\n",
               (unsigned int)(KPO_RD(KPO_BODY_COLOR) & 0x00FFFFFFu),
               (unsigned int)KPO_COLOR_YELLOW);
    xil_printf("  arm  color (R-B-G): %06X  expected cyan=%06X\r\n",
               (unsigned int)(KPO_RD(KPO_ARM_COLOR) & 0x00FFFFFFu),
               (unsigned int)KPO_COLOR_CYAN);
    kpo_print_marker("red ", red);
    kpo_print_marker("blue", blue);
    kpo_print_marker("green", green);

    for (i = 5; i <= 12; ++i) {
        u32 word = KPO_RD(KPO_JOINT0 + ((u32)i * KPO_JOINT_STRIDE));
        xil_printf("  joint[%02d] raw=%08X valid=%lu x=%lu y=%lu group=%s\r\n",
                   i,
                   (unsigned int)word,
                   (unsigned long)((valid >> i) & 1u),
                   (unsigned long)(word & 0x0FFFu),
                   (unsigned long)((word >> 12) & 0x0FFFu),
                   (i == 5 || i == 6 || i == 11 || i == 12) ?
                       "body" : "arm");
    }
}


void kpo_test_group_colors(void)
{
    const u32 red_marker = 0x80000000u | (150u << 11) | 300u;
    const u32 blue_marker = 0x80000000u | (150u << 11) | 980u;

    if (!ready) {
        xil_printf("KPO color test: driver not initialized\r\n");
        return;
    }

    kpo_set_enable(1);
    kpo_set_body_arm_colors(KPO_COLOR_YELLOW, KPO_COLOR_CYAN);
    kpo_set_radius(10u);
    kpo_set_source_frame_id(0xC0100001u);

    /* Body: shoulders and hips. */
    kpo_set_joint(5,  480u, 220u, 127u);
    kpo_set_joint(6,  800u, 220u, 127u);
    kpo_set_joint(11, 540u, 500u, 127u);
    kpo_set_joint(12, 740u, 500u, 127u);

    /* Arms: elbows and wrists. */
    kpo_set_joint(7, 420u, 320u, 127u);
    kpo_set_joint(8, 860u, 320u, 127u);
    kpo_set_joint(9, 360u, 420u, 127u);
    kpo_set_joint(10, 920u, 420u, 127u);

    kpo_set_color_results(red_marker, blue_marker, 0u);
    kpo_set_valid_flags(0x00001FE0u);
    kpo_commit();

    xil_printf("KPO color test requested: body=yellow arms=cyan "
               "red=(300,150) blue=(980,150)\r\n");

    if (kpo_wait_commit(50000000u) == XST_SUCCESS) {
        xil_printf("KPO color test: commit accepted at video SOF\r\n");
    }
    else {
        xil_printf("KPO color test: COMMIT TIMEOUT - check AXI stream SOF\r\n");
    }

    kpo_debug_dump();
}
