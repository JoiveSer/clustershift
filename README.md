<div align="center">

# ⚡ ClusterShift

**High-Performance Native Win32 Storage Optimizer & Cluster Visualizer**  
*Dual-Engine Architecture for Modern NVMe SSDs & Rotational HDDs*

[![CI Build](https://github.com/JoiveSer/clustershift/actions/workflows/ci.yml/badge.svg)](https://github.com/JoiveSer/clustershift/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/JoiveSer/clustershift?color=emerald&label=version)](https://github.com/JoiveSer/clustershift/releases)
[![Platform](https://img.shields.io/badge/platform-Windows%2010%20%7C%2011-blue)](https://github.com/JoiveSer/clustershift)
[![C++](https://img.shields.io/badge/C%2B%2B-20%20%2F%2023-purple)](https://github.com/JoiveSer/clustershift)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Architect](https://img.shields.io/badge/Architect-JoiveSer-cyan)](https://github.com/JoiveSer)

<br/>

```
   ____ _           _            ____  _     _  __ _   
  / ___| |_   _ ___| |_ ___ _ __/ ___|| |__ (_)/ _| |_ 
 | |   | | | | / __| __/ _ \ '__\___ \| '_ \| | |_| __|
 | |___| | |_| \__ \ ||  __/ |   ___) | | | | |  _| |_ 
  \____|_|\__,_|___/\__\___|_|  |____/|_| |_|_|_|  \__|
```

**[ 📥 Download Latest Release (v0.2.0) ](https://github.com/JoiveSer/clustershift/releases/latest)**

</div>

---

## 💡 Overview & Value Proposition

**ClusterShift** is a lightweight, zero-dependency, open-source storage utility engineered in modern C++20/C++23. It replaces outdated, bloated legacy defragmenters with a streamlined **Dual-Engine Pipeline** tailored to modern storage topologies:

* ⚡ **Solid-State Drives (NVMe / SATA SSD):** Active hardware safety intercepts prevent damaging NAND wear. Dispatches instantaneous kernel TRIM (`DeviceDsmAction_Trim`) and extracts low-level S.M.A.R.T. thermal and failure diagnostics.
* 💽 **Mechanical Hard Drives (HDD):** Targeted single-folder and virtual disk consolidation (`FSCTL_MOVE_FILE`) completes in minutes instead of forcing hours of disk-wide head thrashing.
* 🕹️ **Interactive Retro-Modern Visualizer:** Real-time ANSI cluster allocation map rendering millions of NTFS volume clusters directly in your terminal.
* 🚀 **Zero Friction:** Portable 250 KB single executable. Double-click to launch the Interactive Menu, or integrate into the Windows Explorer right-click context menu in 1 click.

---

## ⚖️ Feature Matrix: ClusterShift vs Alternatives

| Feature / Capability | ClusterShift (by Joive) | Microsoft defrag.exe | Sysinternals Contig | Piriform Defraggler |
| :--- | :---: | :---: | :---: | :---: |
| **Open Source (MIT)** | ✅ **Yes** | ❌ Proprietary | ❌ Proprietary | ❌ Abandoned / Proprietary |
| **Targeted Folder Defrag** | ✅ **Yes (Instant)** | ❌ Whole volume only | ✅ Single files | ✅ Folder select |
| **Dual-Engine SSD Protection** | ✅ **Hardware Intercept** | ⚠️ Generic re-trim | ❌ Blind cluster call | ⚠️ Outdated SSD logic |
| **S.M.A.R.T. & Thermal Sensors** | ✅ **Native NVMe Bus** | ❌ No | ❌ No | ⚠️ Basic SATA only |
| **ANSI Terminal Cluster Map** | ✅ **Real-Time Grid** | ❌ None | ❌ None | ⚠️ Heavy Desktop GUI |
| **Interactive Dashboard (No-CLI)** | ✅ **1-Click Menu** | ❌ CLI / MMC only | ❌ CLI only | ✅ GUI only |
| **Explorer Context Menu Integration**| ✅ **Built-in (1-Click)**| ❌ No | ❌ Manual registry | ⚠️ Installer required |
| **Binary Footprint** | ⚡ **< 300 KB (Zero DLLs)**| Integrated in OS | ⚡ ~400 KB | ❌ 15+ MB bloat |

---

## 🏁 Quick Start (30 Seconds)

### Option A: Interactive Dashboard (Double-click friendly)
Simply download and double-click `clustershift.exe` (or run without arguments):

```powershell
.\clustershift.exe
```

```text
=================================================================
  ClusterShift Universal Storage Console [by Joive] - Dashboard 
=================================================================
Detected Storage Volumes:
  [C:] 475.7 GB (42.4 GB free) - SSD/NVMe (WD PC SN740)

Interactive Actions:
  [1] Detailed Storage & S.M.A.R.T. Health Telemetry
  [2] Render Real-Time Cluster Allocation Map
  [3] Dispatch Fast Flash TRIM (SSD/NVMe)
  [4] Scan Folder or Game for Fragmentation
  [5] Add 1-Click Right-Click Context Menu to Windows Explorer
  [6] Remove Explorer Context Menu
  [0] Exit

Select an option (0-6): 2
```

### Option B: Power-User CLI

```powershell
# Query hardware bus, capacity, and geometry
clustershift info C

# Direct S.M.A.R.T. health, serial, and thermal telemetry readout
clustershift health C

# Render real-time ANSI volume cluster bitmap
clustershift map C

# Scan specific folder or game directory recursively
clustershift scan "D:\Games\Cyberpunk" --recursive

# Instantaneous SSD flash maintenance (TRIM)
clustershift trim C

# Dry-run simulate file consolidation (Zero physical disk writes)
clustershift optimize "D:\VirtualMachine.vhdx" --dry-run
```

---

## 🎨 Terminal Visualizer in Action

ClusterShift queries `FSCTL_GET_VOLUME_BITMAP` directly from the NTFS driver and renders an ANSI cluster map across 100M+ clusters in under 50 milliseconds:

```text
=== Volume Cluster Allocation Map for C: ===
+----------------------------------------------------------------+
|████████████████████████████████████████████████████████████████|
|████████████████████████████████████████████████████████████████|
|█████████████████████████████████████████████▒██████████████████|
|███▒██▒████████████████████████▒██▒██████████████████████████▒██|
|████████████████████████▒██▒█▒███████▒█▒██▒███▒███▒█████████████|
|███████████░███▒██████████████████▒█████████████████████████████|
|███▒██████▒███████████░▒█████████▒▒███████▒▒██████▒█▒▒▒█████████|
|████████████████████████████▒██████▒░███▒███████▒██▒▒██▒████████|
+----------------------------------------------------------------+
Legend: █ Allocated   ▒ Partially Free   ░ Contiguous Free Space

  Clusters Total: 124,693,759 | Free Clusters: 11,109,556
```

---

## 🛡️ Architectural Safety Guarantees

<details>
<summary><b>Click to expand low-level architecture details</b></summary>

<br/>

1. **Transactional Kernel Integrity:**  
   ClusterShift never writes raw sectors to physical drive handles. All cluster relocations are executed through the certified Windows NTFS kernel driver (`FSCTL_MOVE_FILE`), ensuring data cannot be corrupted even during unexpected power loss.
2. **Flash Memory Preservation:**  
   Before any consolidation pass, `IOCTL_STORAGE_QUERY_PROPERTY` queries `StorageDeviceSeekPenaltyProperty`. If `IncursSeekPenalty == FALSE` (SSD / NVMe), cluster consolidation is hardware-blocked to prevent burning NAND rewrite cycles (TBW).
3. **Privilege Guard:**  
   Elevates and checks `SE_MANAGE_VOLUME_NAME` token privileges on launch to safely interact with storage management IOCTLs.
4. **Clean Modern C++20/C++23:**  
   Zero raw handle leaks (`ScopedHandle` RAII), structured functional error handling with `std::expected`, and strict 64-bit integer sizing (`LARGE_INTEGER`).

</details>

---

## 🛠️ Building from Source

### Prerequisites
* Windows 10 / Windows 11 x64
* MSVC v143+ (Visual Studio 2022 / BuildTools) with C++20/C++23 support
* CMake 3.20+ (Optional)

### 1-Line Build (MSVC Developer Prompt)
```cmd
cl /std:c++latest /EHsc /O2 /DUNICODE /D_UNICODE /W4 /utf-8 /Iinclude src\core\*.cpp src\main.cpp /Fe:clustershift.exe /link advapi32.lib shell32.lib
```

### CMake Build
```powershell
git clone https://github.com/JoiveSer/clustershift.git
cd clustershift

cmake -B build
cmake --build build --config Release
```

---

## 🗺️ Roadmap

- [x] Native Win32 dual-engine core (SSD TRIM + HDD targeted defrag)
- [x] Low-level S.M.A.R.T. NVMe/SATA telemetry & controller temperature sensors
- [x] Real-time ANSI cluster grid visualizer
- [x] Interactive console dashboard (Double-click support)
- [x] 1-Click Windows Explorer context menu integration
- [x] Automated GitHub Actions CI/CD pipeline
- [ ] Disk I/O read/write throughput benchmark tool
- [ ] Lightweight standalone GUI mode (DirectX 11 + Dear ImGui)

---

## 📜 License & Author

* **Architect & Lead Developer:** **Joive** ([@JoiveSer](https://github.com/JoiveSer))
* **License:** Released under the permissive [MIT License](LICENSE).
