# Camera overlay offline regression

With the .NET 8 SDK installed, run from this directory:

```powershell
dotnet run --project CameraOverlayValidation.csproj
```

This reuses the 43 existing gateway/controller checks from the validated Camera Overlay Toggle R1 package. All byte assertions use in-memory gateways. No SerialPort is opened and no physical UART command is transmitted.

To exercise the actual Windows Player UI, use `../ShareValidation/Invoke-PlayerChecks.ps1` with an explicitly supplied Player executable. Unity's opt-in acceptance scripts remain inactive during normal startup.
