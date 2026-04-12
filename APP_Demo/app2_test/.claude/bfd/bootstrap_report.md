# BFD Project Init Report

- Generated at: 2026-04-12T09:06:07+08:00
- Project root: .
- MCU family: STM32H7
- MCU name: STM32H743IITx
- Debug device: STM32H743IITx

## Artifacts
- IOC: app2_test.ioc
- Startup: MDK-ARM\startup_stm32h743xx.s
- Linker: EIDE\.cmsis\device\ARM\ARMCA5\Source\AC5\ARMCA5.sct
- SVD: 
- ELF: EIDE\build\app2_test\app2_test.elf
- HEX: EIDE\build\app2_test\app2_test.hex
- MAP: EIDE\build\app2_test\app2_test.map

## Gaps
- Blockers: none
### Warnings
- Missing SVD file (*.svd); register capture may be unavailable
- IOC parser export skipped: missing parser script (C:\Users\Rvosy\.claude\.codex\skills\bfd-ioc-parser\scripts\parse_ioc.py)

## Generated Files
- none

## Next Commands
```bash
# Flash
./build_tools/jlink/flash.sh

# RTT
./build_tools/jlink/rtt.sh logs/rtt/bootstrap_check.log 5 --mode quick

# Orchestrated debug campaign
./.codex/skills/bfd-debug-orchestrator/scripts/run_fault_campaign.sh --skip-build
```
