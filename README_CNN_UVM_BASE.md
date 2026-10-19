# CNN UVM Base

This branch is the common UVM foundation for `cnn_accelerator_top`.

## Branch flow

```text
dev/cnn
  -> val/cnn/base
       -> val/cnn/functional
       -> val/cnn/handshake
       -> val/cnn/control
       -> val/cnn/output
```

RTL changes are owned by `dev/cnn`. Verification-common changes are owned by `val/cnn/base`.
Scenario branches should not patch RTL locally; RTL fixes must flow through `dev/cnn` and then be merged into the base branch.

## Current base scope

The base environment models only external ports of `cnn_accelerator_top`:

- AXI4-Lite slave control interface (`s_axil_*`)
- AXI4-Lite master response side (`m_axil_*`) through a placeholder responder
- Image AXI-stream input
- Weight AXI-stream input
- Feature AXI-stream input
- Feature AXI-stream output
- IRQ observation

The master AXI-Lite responder is intentionally a protocol placeholder, not a complete DMA behavioral model.
Control/DMA verification should extend or replace it in `val/cnn/control`.

Internal LineBuffer/DW/PW handshakes are not external top-level ports. Handshake verification can add bind assertions or dedicated passive internal monitors in `val/cnn/handshake`.

## Compile unit convention

The repository keeps `.sv` extensions for UVM class files.
`cnn_base_pkg.sv` includes the class sources, so do not separately compile those included class files.

A VCS-style file list is provided at `CNN/uvm/files.f`.
