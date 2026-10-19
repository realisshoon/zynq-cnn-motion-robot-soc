# CNN-v4.0 Real12 internal accelerator clock constraint.
# The block is implemented out-of-context because its logical SoC interface is
# wider than the bonded package I/O count. Board pin and PS clock constraints
# belong to the parent system project.
create_clock -name core_clk -period 10.000 [get_ports clk]
