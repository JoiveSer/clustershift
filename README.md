# ClusterShift

```
   ____ _           _            ____  _     _  __ _   
  / ___| |_   _ ___| |_ ___ _ __/ ___|| |__ (_)/ _| |_ 
 | |   | | | | / __| __/ _ \ '__\___ \| '_ \| | |_| __|
 | |___| | |_| \__ \ ||  __/ |   ___) | | | | |  _| |_ 
  \____|_|\__,_|___/\__\___|_|  |____/|_| |_|_|_|  \__|
```

> **Universal High-Performance Win32 Storage & Cluster Optimizer**  
> *Architect & Lead Developer:* **Joive**  
> *Target OS:* Windows 10 / Windows 11 (x64) | *Standard:* C++20

---

## Overview

**ClusterShift** is a native, modern storage optimization tool built to replace outdated legacy defragmenters. It introduces a **Dual-Engine Pipeline** tailored for modern systems:

1. **For Solid-State Drives (SSD / NVMe):**
   - **Active Hardware Safety Intercept:** Prevents harmful cluster reshuffling that burns NAND flash endurance.
   - **Fast DSM TRIM Dispatch:** Uses Windows storage management IOCTLs (`DeviceDsmAction_Trim`) to trigger immediate controller block garbage collection.
   - **S.M.A.R.T. & Thermal Telemetry:** Direct hardware bus identification (NVMe PCIe vs SATA), failure prediction, and controller thermal monitoring.

2. **For Rotational Hard Drives (HDD):**
   - **Targeted Single-File / Directory Optimization:** Defragments specific games or virtual disks in 1–2 minutes instead of forcing hours of disk-wide thrashing.
   - **Direct Kernel Cluster Relocation:** Uses native NTFS driver primitives (`FSCTL_MOVE_FILE`, `FSCTL_GET_VOLUME_BITMAP`, `FSCTL_GET_RETRIEVAL_POINTERS`).
   - **Simulation (`--dry-run`):** Accurately tests and visualizes cluster consolidation without modifying physical sectors.

3. **Retro-Modern ANSI Visualizer:**
   - Real-time terminal cluster allocation grid (reminiscent of classic Windows 98 defrag, rendered in modern ANSI colors).

---

## CLI Reference

```powershell
# Query volume architecture and media type (SSD vs HDD)
clustershift info C

# Check drive health, bus interface (NVMe/SATA), and controller temperature
clustershift health C

# Render real-time ANSI cluster allocation map
clustershift map C

# Scan file or directory for fragmentation metrics and extents
clustershift scan "D:\Games\Cyberpunk" --recursive

# Dispatch instantaneous TRIM to restore SSD write performance
clustershift trim C

# Safely simulate file consolidation (Dry-run, zero physical writes)
clustershift optimize "D:\HeavyDatabase.vhdx" --dry-run

# Execute live cluster consolidation (HDD only)
clustershift optimize "D:\HeavyDatabase.vhdx"
```

---

## Architectural Guarantees & Safety

* **Transactional Kernel Safety:** ClusterShift never writes raw sectors to physical disks. All cluster moves are delegated to the Windows NTFS driver (`FSCTL_MOVE_FILE`), ensuring filesystem integrity even across unexpected power loss.
* **Privilege Guard:** Elevates `SE_MANAGE_VOLUME_NAME` at startup to authorize filesystem control codes.
* **Production-Grade Architecture:** Clean modern C++20/C++23 RAII wrappers, structured error handling (`std::expected`), strict 64-bit cleanliness, zero raw handle leaks.

---

## Building from Source

### Prerequisites
* Windows 10 / Windows 11 x64
* Visual Studio 2022+ (MSVC v143+) or Clang-cl with C++20 support
* CMake 3.20+

### Build Steps
```powershell
git clone https://github.com/JoiveSer/clustershift.git
cd clustershift

cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

The compiled binary will be located in `build/Release/clustershift.exe`.

---

## License & Credits

Designed, architected, and maintained by **Joive**.  
Open-source under the MIT License.
