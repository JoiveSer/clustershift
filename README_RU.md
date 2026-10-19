<div align="center">

# ⚡ ClusterShift

**Универсальный высокопроизводительный Win32-комплекс для накопителей и аппаратной телеметрии**  
*Двухкомпонентная архитектура (консольный CLI + DirectX 11 GUI) • Диагностика DirectStorage BypassIO • Многопоточный бенчмарк IOCP*

[![CI Build](https://github.com/JoiveSer/clustershift/actions/workflows/ci.yml/badge.svg)](https://github.com/JoiveSer/clustershift/actions/workflows/ci.yml)
[![Version](https://img.shields.io/badge/version-v0.6.0-emerald)](https://github.com/JoiveSer/clustershift/releases)
[![Platform](https://img.shields.io/badge/platform-Windows%2010%20%7C%2011-blue)](https://github.com/JoiveSer/clustershift)
[![C++](https://img.shields.io/badge/C%2B%2B-20%20%2F%2023-purple)](https://github.com/JoiveSer/clustershift)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Architect](https://img.shields.io/badge/Architect-JoiveSer-cyan)](https://github.com/JoiveSer)

<p align="center">
  <a href="README.md">English</a> | <b>Русский</b>
</p>

<br/>

```
   ____ _           _            ____  _     _  __ _   
  / ___| |_   _ ___| |_ ___ _ __/ ___|| |__ (_)/ _| |_ 
 | |   | | | | / __| __/ _ \ '__\___ \| '_ \| | |_| __|
 | |___| | |_| \__ \ ||  __/ |   ___) | | | | |  _| |_ 
  \____|_|\__,_|___/\__\___|_|  |____/|_| |_|_|_|  \__|
```

**[ 📥 Скачать свежий релиз (v0.6.0) ](https://github.com/JoiveSer/clustershift/releases/latest)**

</div>

---

## 💡 Обзор и ключевые возможности

**ClusterShift** — это легковесная, открытая и полностью автономная утилита без внешних зависимостей, написанная на современном C++20/C++23. Проект поставляется в виде **двух специализированных исполняемых файлов**: сверхбыстрого консольного CLI (`clustershift.exe`, < 430 КБ) и графической оболочки с аппаратным ускорением (`clustershift-gui.exe`, < 1 МБ):

* 🖥️ **Двухкомпонентная дистрибуция (CLI + GUI):**
  * **Headless CLI (`clustershift.exe`):** Молниеносный консольный инструмент с интерактивными ASCII-дашбордами, нулевым оверхедом, идеален для автоматизации, скриптов и терминальных энтузиастов.
  * **Аппаратный GUI (`clustershift-gui.exe`):** Интерфейс на DirectX 11 + Dear ImGui в стиле аутентичной Windows XP Luna с экраном загрузки, сглаженными шрифтами, 3D-диаграммами накопителей и 0% нагрузки на CPU в простое.
* ⚡ **Параллельный многопоточный бенчмарк IOCP (`bench`):** Прямой ввод-вывод с выравниванием по секторам (`FILE_FLAG_NO_BUFFERING`) на базе портов завершения ввода-вывода Windows (`GetQueuedCompletionStatusEx`), масштабируемый по всем ядрам CPU (`Q32Tx`). Выдает свыше **334 000+ IOPS** и 2.5+ ГБ/с без искажений системного файлового кэша Windows.
* ⏱️ **Субмикросекундный профилировщик задержки (Q1T1):** Замеряет реальную однопоточную задержку чтения блоков 4КБ в микросекундах ($\mu\text{s}$) с мин/сред/макс метриками — решающий фактор скорости отклика Windows и загрузки игровых ассетов.
* 🎮 **Телеметрия DirectStorage / BypassIO (`health`, `info`):** Опрашивает состояние Windows 11 BypassIO (`FSCTL_MANAGE_BYPASS_IO`), определяя, активна ли прямая потоковая передача данных из NVMe в видеопамять GPU, или её блокируют драйверы фильтров антивирусов (`.sys`).
* 🔍 **Адаптивный гибридный сканер диска (`hogs`):** Аппаратно-зависимый двухрежимный движок — параллельное сканирование по всем ядрам на NVMe/SSD и строго однопоточный последовательный обход на HDD (`IncursSeekPenalty`), предотвращающий износ магнитных головок.
* 🛡️ **Защита SSD и мгновенный TRIM (`trim`):** Аппаратные блокировки предотвращают износ ячеек памяти NAND. Отправляет мгновенную команду ядра TRIM (`DeviceDsmAction_Trim`) для восстановления пиковой скорости записи.
* 💽 **Физически-обоснованная оптимизация HDD (`optimize`):**
  * **Фильтр крупных экстентов (`--min-frag-size`):** Пропускает фрагменты $\ge 50\text{ МБ}$, где линейная скорость перевешивает задержку позиционирования головки, сокращая 70–90% паразитного износа механизма.
  * **Приоритет зонной записи (ZBR):** Перемещает файлы в начальные сектора LCN (внешние дорожки пластин) для достижения максимальной линейной скорости (220–260 МБ/с).
  * **Фоновое планирование ввода-вывода:** Переводит поток в режим `THREAD_MODE_BACKGROUND_BEGIN` (`IoPriorityHintVeryLow`), снижая приоритет очереди диска до минимального фонового, предотвращая просадки FPS в играх.
* 🕹️ **Интерактивная карта кластеров (`map`):** Визуализация миллионов кластеров тома NTFS в реальном времени с защитой зоны главной файловой таблицы `$MFT`.

---

## ⚖️ Сравнительная матрица: ClusterShift и аналоги

| Функция / Возможность | ClusterShift (by Joive) | CrystalDiskMark | WizTree / TreeSize | Встроенный defrag |
| :--- | :---: | :---: | :---: | :---: |
| **Открытый исходный код (MIT)** | ✅ **Да** | ⚠️ Ограничен | ❌ Проприетарный | ❌ Закрытый код |
| **Форм-фактор (CLI + GUI)** | ✅ **Оба в комплекте** | ❌ Только GUI | ❌ Только GUI | ⚠️ Только CLI |
| **Многоочередной Direct I/O (Q32Tx)** | ✅ **Да (334k+ IOPS)** | ✅ Да (Q32T16) | ❌ Нет | ❌ Нет |
| **Субмикросекундная задержка (Q1T1)** | ✅ **Да (Мин/Сред/Макс $\mu\text{s}$)** | ⚠️ Только средняя | ❌ Нет | ❌ Нет |
| **Телеметрия DirectStorage / BypassIO** | ✅ **Да (Kernel IOCTL)** | ❌ Нет | ❌ Нет | ❌ Нет |
| **Быстрый анализатор места** | ✅ **Да (Адаптивный)** | ❌ Нет | ✅ Да (через $MFT) | ❌ Нет |
| **Аппаратная защита ячеек SSD** | ✅ **Да** | ➖ Н/Д | ➖ Н/Д | ⚠️ Обычный re-trim |
| **ZBR-оптимизация (внешние дорожки)** | ✅ **Да (Low-LCN Priority)** | ❌ Нет | ❌ Нет | ❌ Нет |
| **Пропуск крупных фрагментов** | ✅ **Да (--min-frag-size)** | ❌ Нет | ❌ Нет | ❌ Слепой прогон |
| **S.M.A.R.T. NVMe/SATA телеметрия** | ✅ **Прямой IOCTL** | ❌ В отдельной утилите | ❌ Нет | ❌ Нет |
| **Интерактивная карта кластеров** | ✅ **2D-холст кластеров** | ❌ Нет | ❌ Тяжелый GUI | ❌ Нет |
| **Интеграция в контекстное меню** | ✅ **Встроена** | ❌ Нет | ⚠️ Нужен инсталлятор | ❌ Нет |
| **Размер бинарников и зависимости** | ⚡ **< 430 КБ CLI / < 1 МБ GUI** | ❌ 10+ МБ (Electron/GUI) | ❌ 15+ МБ | Встроен в ОС |

---

## 🏁 Быстрый старт (Quick Start)

### Вариант А: Графический интерфейс (`clustershift-gui.exe`)
Скачайте и запустите `clustershift-gui.exe` (или введите `clustershift gui` в терминале):

* Плавный 60+ FPS интерфейс на DirectX 11 с аутентичной темой Windows XP Luna.
* 0% нагрузки на процессор в состоянии покоя благодаря адаптивному таймингу событий.
* Живой дашборд накопителя, бенчмарк, очистка диска, карта кластеров и 3D-диаграммы.

### Вариант Б: Интерактивный консольный дашборд
Дважды кликните по `clustershift.exe` или запустите без параметров в PowerShell:

```powershell
.\clustershift.exe
```

### Вариант В: Команды CLI для терминала

```powershell
# Запуск графического интерфейса
clustershift gui

# Прямой аппаратный бенчмарк (Многопоточный IOCP + задержка Q1T1)
clustershift bench C
clustershift bench C --quick

# Адаптивное сканирование диска на самые тяжелые файлы и папки
clustershift hogs C:\
clustershift hogs "D:\Games" --top 20

# Запрос шины накопителя, геометрии и статуса DirectStorage
clustershift info C

# Прямая телеметрия S.M.A.R.T., серийный номер, температура и BypassIO
clustershift health C

# Отображение ANSI-карты кластеров тома в реальном времени
clustershift map C

# Поиск фрагментов файлов в конкретной папке или игре
clustershift scan "D:\Games\Cyberpunk" --recursive

# Мгновенная очистка ячеек flash-памяти SSD (TRIM)
clustershift trim C

# Физически-грамотная оптимизация HDD (пропуск фрагментов >= 50МБ, укладка на ZBR)
clustershift optimize "D:\VirtualMachine.vhdx" --min-frag-size 50
clustershift optimize "D:\VirtualMachine.vhdx" --dry-run
```

---

## 📊 Примеры вывода в терминале

<details open>
<summary><b>1. Аппаратный многопоточный бенчмарк IOCP (<code>bench</code>)</b></summary>

```text
+--------------------------------------------------------------------------------+
|                 ClusterShift Hardware Storage Benchmark Results                |
+--------------------------------------------------------------------------------+
  Test Target:     Drive C:\ (Unbuffered Direct I/O)
  Sample Size:     1024 MB

  Sequential Write (1MB):    2072.2 MB/s
  Sequential Read  (1MB):    2543.4 MB/s
  Random Read 4KB (Q32T16):   334919 IOPS (1308.3 MB/s)
  Random Read 4KB (Q1T1):      14060 IOPS (54.9 MB/s | Latency: 70.5 us [min 51.1 us / max 4049.0 us])

  Queue Depth:     32 (IOCP Multi-Threaded Batch Dequeue)
  Worker Threads:  16 concurrent kernel completions
  Total Duration:  1.14 seconds
+--------------------------------------------------------------------------------+
```
</details>

<details>
<summary><b>2. Телеметрия контроллера, S.M.A.R.T. и DirectStorage BypassIO (<code>health</code>)</b></summary>

```text
=== Storage Controller & S.M.A.R.T. Health ===
  Interface Bus:   NVMe (PCIe Express)
  Vendor:          NVMe
  Model / Product: WD PC SN740 SDDQNQD-512G-1014
  Serial Number:   E823_8FA6_BF53_0001_001B_448B_4CB1_A893.
  Failure Status:  [HEALTHY - No hardware faults flagged]
  Thermal Sensor:  39 °C (Normal)
  DirectStorage:   [BYPASS_IO OPTIMAL - Hardware GPU Asset Streaming Ready]
```
</details>

<details>
<summary><b>3. Быстрый анализ занятого пространства (<code>hogs</code>)</b></summary>

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
```
</details>

---

## 🛡️ Низкоуровневые архитектурные гарантии

<details>
<summary><b>Развернуть детальное описание архитектуры ядра</b></summary>

<br/>

1. **Выравнивание Direct I/O по секторам и пайплайн IOCP:**  
   Бенчмарк выделяет память через `VirtualAlloc(PAGE_READWRITE)` строго с выравниванием по границам секторов 4КБ, открывая хэндлы с флагами `FILE_FLAG_NO_BUFFERING | FILE_FLAG_OVERLAPPED`. Порт завершения ввода-вывода (**IOCP**) с вызовом `GetQueuedCompletionStatusEx` пакетно забирает результаты операций на нескольких рабочих потоках, полностью насыщая очереди современных NVMe-контроллеров (`Q32Tx`). Флаг `FILE_FLAG_DELETE_ON_CLOSE` гарантирует автоматическое удаление временных файлов даже при аварийном завершении.
2. **Защита зоны резервирования $MFT в NTFS:**  
   Перед выделением свободных кластеров ClusterShift запрашивает `FSCTL_GET_NTFS_VOLUME_DATA`, определяя границы `MftZoneStart` и `MftZoneEnd`. Алгоритм намертво блокирует запись в зарезервированную системную зону `$MFT` (12.5% емкости диска), предотвращая фрагментацию таблицы файлов системы, оставляя внешние кластеры доступными для размещения данных.
3. **Физически-адаптивный движок (HDD против SSD):**  
   - **Механические HDD (Активен Seek Penalty):** Сканер диска (`hogs`) принудительно переключается в однопоточный последовательный режим для устранения задержек перемещения магнитной головки (4–12 мс на прыжок). Оптимизация игнорирует фрагменты $\ge 50\text{ МБ}$ (`--min-frag-size`) и укладывает файлы на внешние дорожки (младшие LCN) для максимальной линейной скорости (220–260 МБ/с).
   - **Flash SSD/NVMe (Нулевой Seek Penalty):** Задействует параллельный многопоточный опрос всех ядер CPU и блокирует дефрагментацию в пользу отправки прямого системного сигнала TRIM (`DeviceDsmAction_Trim`).
4. **Фоновое планирование ввода-вывода (`THREAD_MODE_BACKGROUND_BEGIN`):**  
   Операции переноса кластеров выполняются в режиме `THREAD_MODE_BACKGROUND_BEGIN`, понижая приоритеты ядра Windows как для CPU, так и для дисковых очередей до уровня `Very Low` (`IoPriorityHintVeryLow`). Это гарантирует полное отсутствие просадок частоты кадров (FPS) в играх и задержек в работе открытых приложений.
5. **Телеметрия DirectStorage BypassIO:**  
   Напрямую отправляет управляющий код `FSCTL_MANAGE_BYPASS_IO` в стек устройств тома NTFS, извлекая системные коды ошибок и имена драйверов-фильтров, проверяя готовность накопителя к прямой высокоскоростной загрузке игровых ассетов в видеокарту.

</details>

---

## 🛠️ Сборка из исходников

### Требования
* Windows 10 / Windows 11 x64
* MSVC v143+ (Visual Studio 2022 / 2026 BuildTools) с поддержкой стандартов C++20/C++23
* DirectX 11 SDK (входит в стандартный Windows 10/11 SDK)

### Быстрая сборка в одну команду (Developer Command Prompt)
```cmd
# 1. Сборка консольного CLI
cl /std:c++latest /EHsc /O2 /DUNICODE /D_UNICODE /W4 /utf-8 /Iinclude src\core\*.cpp src\main.cpp /Fe:clustershift.exe /link advapi32.lib shell32.lib

# 2. Сборка графической оболочки DirectX 11
cl /std:c++latest /EHsc /O2 /DUNICODE /D_UNICODE /utf-8 /Iinclude /Ithird_party/imgui /Ithird_party/imgui/backends src/core/*.cpp third_party/imgui/imgui*.cpp third_party/imgui/backends/imgui_impl_win32.cpp third_party/imgui/backends/imgui_impl_dx11.cpp src/gui/gui_main.cpp /Fe:clustershift-gui.exe /link /subsystem:windows d3d11.lib dxgi.lib dwmapi.lib advapi32.lib shell32.lib user32.lib gdi32.lib
```

### Сборка через CMake
```powershell
git clone https://github.com/JoiveSer/clustershift.git
cd clustershift

cmake -B build
cmake --build build --config Release
```

---

## 🗺️ План развития (Roadmap)

- [x] Двухрежимное Win32-ядро (TRIM для SSD + точечная оптимизация HDD)
- [x] Низкоуровневая телеметрия S.M.A.R.T. NVMe/SATA и термодатчики контроллеров
- [x] Диагностика драйверов-фильтров DirectStorage / BypassIO
- [x] Параллельный многопоточный бенчмарк IOCP (Q32Tx 334k+ IOPS) + монитор задержки Q1T1
- [x] Визуализатор карты кластеров в ANSI-графике терминала
- [x] Интерактивный консольный дашборд
- [x] Автономная графическая оболочка на DirectX 11 (`clustershift-gui.exe`)
- [x] Интеграция команд в контекстное меню Проводника Windows в 1 клик
- [x] Адаптивный сканер пространства (многопоточный для SSD / однопоточный для HDD)
- [x] Фильтр крупных фрагментов (`--min-frag-size`) и оптимизация ZBR
- [x] Автоматизированный пайплайн сборки GitHub Actions CI/CD
- [ ] Прямой парсер бинарной таблицы `$MFT` для мгновенной каталогизации диска < 0.1с
- [ ] Официальные пакеты для WinGet и Scoop

---

## 📜 Лицензия и автор

* **Архитектор и ведущий разработчик:** **Joive** ([@JoiveSer](https://github.com/JoiveSer))
* **Ko-fi:** [https://ko-fi.com/joive](https://ko-fi.com/joive)
* **Лицензия:** Распространяется под свободной лицензией [MIT License](LICENSE).

---

## ☕ Поддержка проекта

Если **ClusterShift** оказался полезен для оптимизации или тестирования накопителей, вы можете поддержать разработку:  
👉 **[Купить автору кофе на Ko-fi (https://ko-fi.com/joive)](https://ko-fi.com/joive)**
