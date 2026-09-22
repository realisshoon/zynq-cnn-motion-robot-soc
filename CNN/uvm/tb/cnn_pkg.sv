package gpio_pkg;
    import uvm_pkg::*;
    `include "uvm_macros.svh"

    // 의존성 순서대로 include 작성
    // compile이 위에부터 읽기 때문에
    `include "gpio_seq_item.sv"
    `include "gpio_sequence.sv"
    `include "gpio_driver.sv"
    `include "gpio_monitor.sv"
    `include "gpio_agent.sv"
    `include "gpio_scoreboard.sv"
    `include "gpio_coverage.sv"
    `include "gpio_env.sv"
    `include "gpio_test.sv"

endpackage
