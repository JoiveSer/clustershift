<div align="center">

# ⚡ ClusterShift

**Universal High-Performance Win32 Storage Suite & Hardware Telemetry**  
*Storage Optimization • Unbuffered Hardware Benchmarking • Space-Hog Analysis • Dual-Engine Maintenance*

[![CI Build](https://github.com/JoiveSer/clustershift/actions/workflows/ci.yml/badge.svg)](https://github.com/JoiveSer/clustershift/actions/workflows/ci.yml)
[![Version](https://img.shields.io/badge/version-v0.3.0-emerald)](https://github.com/JoiveSer/clustershift/releases)
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

**[ 📥 Download Latest Release (v0.3.0) ](https://github.com/JoiveSer/clustershift/releases/latest)**

</div>

---

## 💡 Overview & Value Proposition

**ClusterShift** is a lightweight, zero-dependency, open-source storage utility engineered in modern C++20/C++23. It replaces multiple bulky, closed-source storage tools (defragmenters, disk speed testers, disk space analyzers) with a single, lightning-fast native binary (< 350 KB):

* ⚡ **Unbuffered Storage Benchmark (`bench`):** Sector-aligned Direct I/O (`FILE_FLAG_NO_BUFFERING`) with **Queue Depth 32 (QD32)** asynchronous overlapped I/O, measuring true Sequential Read/Write throughput and Random 4KB IOPS without OS cache distortion.
* 🔍 **High-Speed Multi-Core Space Scanner (`hogs`):** Parallel directory crawling across CPU cores using Win32 `FIND_FIRST_EX_LARGE_FETCH` to pinpoint top largest files, folder capacity hierarchy, and extension footprints in fractions of a second.
* 🛡️ **Dual-Engine SSD Protection & TRIM (`trim`):** Active hardware safety intercepts prevent damaging NAND wear. Dispatches instantaneous kernel TRIM (`DeviceDsmAction_Trim`) for peak flash write IOPS.
* 💽 **Physics-Aware HDD Optimization (`optimize`):**
  * **Large Extent Filter (`--min-frag-size`):** Skips extents $\ge 50\text{ MB}$ where linear streaming transfer dominates mechanical seek latency, eliminating 70–90% of redundant head wear.
  * **ZBR (Zone-Bit Recording) Prioritization:** Relocates files into the lowest LCN ranges (outer platter cylinders) for maximum linear throughput (220–260 MB/s).
  * **Background I/O Scheduling:** Lowers thread priority (`THREAD_PRIORITY_BELOW_NORMAL`) to eliminate system and gaming frame drops during maintenance.
* 🕹️ **Interactive Retro-Modern Visualizer (`map`):** Real-time ANSI cluster allocation map rendering millions of NTFS volume clusters directly in your terminal.
* 🚀 **Zero Friction:** Portable single executable. Double-click to launch the Interactive Menu, or integrate into the Windows Explorer right-click context menu in 1 click.

---

## ⚖️ Feature Matrix: ClusterShift vs Alternatives

| Feature / Capability | ClusterShift (by Joive) | CrystalDiskMark | WizTree / TreeSize | Microsoft defrag |
| :--- | :---: | :---: | :---: | :---: |
| **Open Source (MIT)** | ✅ **Yes** | ⚠️ Source restricted | ❌ Proprietary | ❌ Closed Source |
| **Direct Hardware I/O Benchmark** | ✅ **Yes (QD32 Async I/O)**| ✅ Yes (QD32) | ❌ No | ❌ No |
| **Fast Space-Hog Analyzer** | ✅ **Yes (Multi-Core Crawl)**| ❌ No | ✅ Yes ($MFT engine) | ❌ No |
| **Dual-Engine SSD Wear Intercept**| ✅ **Hardware Guard** | ➖ N/A | ➖ N/A | ⚠️ Generic re-trim |
| **ZBR Outer-Platter Optimization**| ✅ **Low-LCN Priority** | ❌ No | ❌ No | ❌ No |
| **Large Fragment Skip Filter** | ✅ **Yes (--min-frag-size)**| ❌ No | ❌ No | ❌ Whole file blind |
| **S.M.A.R.T. NVMe/SATA Health** | ✅ **Direct IOCTL** | ❌ Separate app | ❌ No | ❌ No |
| **ANSI Terminal Cluster Map** | ✅ **Real-Time Grid** | ❌ No | ❌ Heavy GUI | ❌ No |
| **1-Click Explorer Context Menu** | ✅ **Built-in** | ❌ No | ⚠️ Installer required | ❌ No |
| **Binary Footprint & Dependencies**| ⚡ **< 350 KB (0 DLLs)** | ❌ 10+ MB (Electron/GUI) | ❌ 15+ MB | Integrated in OS |

---

## 🏁 Quick Start

### Option A: Interactive Dashboard (Double-click friendly)
Simply download and double-click `clustershift.exe` (or run without arguments in PowerShell/Terminal):

```powershell
.\clustershift.exe
```

```text
=================================================================
  ClusterShift Universal Storage Console [by Joive] — Dashboard 
=================================================================
Detected Storage Volumes:
  [C:] 475.7 GB (42.4 GB free) — SSD/NVMe (WD PC SN740)

Interactive Actions:
  [1] Detailed Storage & S.M.A.R.T. Health Telemetry
  [2] Render Real-Time Cluster Allocation Map
  [3] Dispatch Fast Flash TRIM (SSD/NVMe)
  [4] Scan Folder or Game for Fragmentation
  [5] Run Storage Benchmark (QD32 Async 4K IOPS)
  [6] Space-Hog Analyzer (Find Largest Files & Folders)
  [7] Add 1-Click Right-Click Context Menu to Windows Explorer
  [8] Remove Explorer Context Menu
  [0] Exit

Select an option (0-8):
```

### Option B: Power-User CLI

```powershell
# Direct unbuffered hardware benchmark (Standard 1GB sample, QD32 Overlapped Async I/O)
clustershift bench C
clustershift bench C --quick

# Parallel multi-core space scan for largest files, directories & extensions
clustershift hogs C:\
clustershift hogs "D:\Games" --top 20

# Query hardware bus, capacity, and geometry
clustershift info C

# Direct S.M.A.R.T. health, serial, and thermal telemetry readout
clustershift health C

# Render real-time ANSI volume cluster bitmap
clustershift map C

# Scan specific folder or game directory for file fragments
clustershift scan "D:\Games\Cyberpunk" --recursive

# Instantaneous SSD flash maintenance (TRIM)
clustershift trim C

# Physics-aware file consolidation (skips fragments >= 50MB, targets ZBR outer cylinders)
clustershift optimize "D:\VirtualMachine.vhdx" --min-frag-size 50
clustershift optimize "D:\VirtualMachine.vhdx" --dry-run
```

---

## 📊 Live Terminal Output Examples

### 1. Unbuffered Hardware Storage Benchmark (`bench`)
```text
+--------------------------------------------------------------+
|          ClusterShift Hardware Storage Benchmark Results     |
+--------------------------------------------------------------+
  Test Target:     Drive C:\ (Unbuffered Direct I/O)
  Sample Size:     1024 MB

  Sequential Write (1MB):   3005.0 MB/s
  Sequential Read  (1MB):   2746.6 MB/s
  Random Read 4KB (QD32):    24762 IOPS (96.7 MB/s)

  Queue Depth:     32 (Overlapped Async I/O)
  Total Duration:  4.29 seconds
+--------------------------------------------------------------+
```

### 2. High-Speed Space-Hog Analysis (`hogs`)
```text
=== Space-Hog Analysis: C:\Users\Joive\ ===
  Scanned: 142,500 files across 18,200 directories in 0.85s
  Total Aggregate Size: 182.4 GB

[ Top Largest Files ]
  1. [  14.20 GB ]  C:\Users\Joive\VirtualBox VMs\Windows11.vdi
  2. [   8.50 GB ]  C:\Users\Joive\Downloads\ubuntu-24.04-desktop-amd64.iso
  3. [   4.10 GB ]  C:\Users\Joive\Videos\Recording_2026.mp4

[ Top Subfolders by Capacity ]
  1. [  92.40 GB ]  C:\Users\Joive\VirtualBox VMs
  2. [  35.10 GB ]  C:\Users\Joive\AppData
  3. [  24.50 GB ]  C:\Users\Joive\Downloads

[ Top Extensions by Footprint ]
           .vdi :   92.40 GB (2 files)
           .iso :   18.20 GB (3 files)
           .mp4 :   14.50 GB (18 files)
```

### 3. Real-Time ANSI Cluster Allocation Map (`map`)
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

1. **Direct I/O Sector Alignment & QD32 Async Pipeline:**  
   The benchmark allocates memory via `VirtualAlloc(PAGE_READWRITE)` strictly aligned to 4KB sector boundaries, opening handles with `FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH | FILE_FLAG_OVERLAPPED`. A 32-slot event pipeline saturates the NVMe controller queue depth, while `FILE_FLAG_DELETE_ON_CLOSE` guarantees automatic cleanup of temporary files.
2. **Physics-Aware HDD Protection:**  
   Fragment re-allocation respects physical platter constraints. Extents exceeding `--min-frag-size` (default: 50MB) are bypassed because sequential mechanical streaming already dominates seek time. Relocations prioritize outer platter cylinders (low LCNs) for peak linear bandwidth (220–260 MB/s).
3. **Background I/O Scheduling:**  
   File movement passes run under `THREAD_PRIORITY_BELOW_NORMAL` to prevent system and game stuttering.
4. **Flash Memory Preservation:**  
   Before any consolidation pass, `IOCTL_STORAGE_QUERY_PROPERTY` queries `StorageDeviceSeekPenaltyProperty`. If `IncursSeekPenalty == FALSE` (SSD / NVMe), cluster consolidation is hardware-blocked to prevent burning NAND rewrite cycles (TBW).
5. **Transactional Kernel Integrity:**  
   ClusterShift never writes raw sectors to physical drive handles. All cluster relocations are executed through the certified Windows NTFS kernel driver (`FSCTL_MOVE_FILE`), ensuring data cannot be corrupted even during unexpected power loss.

</details>

---

## 🛠️ Building from Source

### Prerequisites
* Windows 10 / Windows 11 x64
* MSVC v143+ (Visual Studio 2022 / 2026 BuildTools) with C++20/C++23 support
* CMake 3.20+ (Optional)

### 1-Line Build (MSVC Developer Command Prompt)
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
- [x] Disk I/O unbuffered throughput benchmark (Seq & QD32 Async 4K IOPS)
- [x] Multi-core parallel space-hog analyzer
- [x] Physics-aware large fragment filter (`--min-frag-size`) & ZBR outer-cylinder placement
- [x] Automated GitHub Actions CI/CD pipeline
- [ ] Raw `$MFT` table direct parser engine for instant sub-0.1s volume cataloging
- [ ] Lightweight standalone GUI mode (DirectX 11 + Dear ImGui)

---

## 📜 License & Author

* **Architect & Lead Developer:** **Joive** ([@JoiveSer](https://github.com/JoiveSer))
* **License:** Released under the permissive [MIT License](LICENSE).
