// ============================================================
// Include Directories
// ============================================================

+incdir+../common
+incdir+.
+incdir+./items
+incdir+./drivers
+incdir+./responders
+incdir+./monitors
+incdir+./checkers
+incdir+./coverage
+incdir+./sequences
+incdir+./tests
+incdir+../../rtl


// ============================================================
// RTL Library
// Read only
// ============================================================

-y ../../rtl
+libext+.v


// ============================================================
// Common UVM Infrastructure
// Read only
// ============================================================

../common/cnn_if.sv
../pkg/cnn_base_pkg.sv


// ============================================================
// Control Verification Package
// ============================================================

./cnn_ctrl_pkg.sv

// C05 controller invariants are bound without modifying RTL.
./assertions/cnn_c05_fsm_sva.sv
./assertions/cnn_c06_reset_sva.sv
./assertions/cnn_c07_irq_sva.sv
./assertions/cnn_c08_fault_sva.sv


// ============================================================
// Testbench Top
// Read only
// ============================================================

../tb_top.sv
