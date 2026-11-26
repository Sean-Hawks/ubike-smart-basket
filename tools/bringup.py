#!/usr/bin/env python3
"""Explicit, one-module-at-a-time PlatformIO upload and serial monitor helper."""

import argparse
import json
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
PARTS = {
    "oled": ("hwtest_oled", "3.3 V；SDA13/SCL14；SH1106 0x3C"),
    "inputs": ("hwtest_inputs", "蓋子 GPIO33、按鈕 GPIO32；導通接 GND"),
    "tof": ("hwtest_tof", "3.3 V；TCA9548A 通道 0/1 各一顆 VL53L0X"),
    "hx711": ("hwtest_hx711", "5 V；DT16/SCK17 經轉換；t → c 200 → s"),
    "ir": ("hwtest_ir", "全3.3V相容性初測；27→1kΩ→DAT、OUT→34；商品標5V，接法見 WIRING.md"),
    "nfc": ("hwtest_nfc", "PN532 V3：VCC5V；SPI SCK18/MISO19/MOSI23/SS5，訊號3.3V"),
    "buzzer": ("hwtest_buzzer", "GPIO25 經 NPN 驅動；蜂鳴器中間腳不接"),
    "lock": ("hwtest_lock", "GPIO26 經功率 MOSFET；u=500 ms；先讀 WIRING.md 第 8 節"),
    "main": ("esp32dev", "所有單體通過後；SmartBasket / 密碼見 USB 序列埠 / 192.168.4.1"),
}

LABELS = {
    "oled": "OLED 螢幕（建議先測）", "inputs": "蓋子開關與開蓋按鈕",
    "tof": "兩顆測距＋TCA 分接板", "hx711": "秤重棒＋HX711 校正",
    "ir": "紅外線發射／接收", "nfc": "NFC 讀卡板",
    "buzzer": "蜂鳴器", "lock": "電控鎖（最後測）", "main": "整合主程式（單體通過後）",
}

GUIDES = {
    "oled": (
        "只需接螢幕：VCC→3V3、GND→GND、SDA→13、SCL→14。",
        "不用輸入指令，每 3 秒依序顯示文字、粗框＋白塊、整片點亮、全黑；測試用 50 kHz。",
        "0x3C 有回應只是通訊檢查；還要文字清楚、粗框／白塊完整、整片點亮無黑條、全黑無殘影。",
    ),
    "inputs": (
        "蓋子開關接 33 與 GND；按鈕接 32 與 GND。不需接鎖。",
        "開／關蓋、按下／放開按鈕各 20 次。可先用跳線接 GND 模擬。",
        "關蓋顯示關上、開蓋顯示打開；按鈕每次只有一次按下／放開。",
    ),
    "tof": (
        "TCA SDA→21、SCL→22；兩顆測距分接 SD0/SC0、SD1/SC1；全部 3.3V、共地。",
        "先擋第一顆，再擋第二顆；量測時讓物品位在約 10–50 cm。",
        "TCA 為 0x70，兩路各有 0x29；遮住哪顆，只有對應距離變短，無連續逾時。",
    ),
    "hx711": (
        "HX711 5V；DT 經 BSS138→16、17 經 AHCT125→SCK；秤重棒紅E+黑E−綠A+白A−。",
        "清空輸入 t；放 200g 已知重量輸入 c 200；確認後輸入 s。每次按 Enter。",
        "儲存成功且重開機仍保留；200g 誤差目標≤5g，空籃 60 秒約在±3g 內。",
    ),
    "ir": (
        "無轉換板時先全3.3V初測：兩片VCC→3V3、GND共地、GPIO27→1kΩ→DAT、OUT→GPIO34。商品標5V，此模式相容性待測；5V接法見 WIRING.md。",
        "先相距 5–10cm 對準，再用不透光物完全擋住；交替測 20 次，通過後再增加到實際籃寬。",
        "對準顯示通、4/4；遮住顯示擋住、0/4；未發射時 LOW 的異常不能當通過。3.3V收不到時先查相容性，OUT 直連時維持兩片3.3V供電。",
    ),
    "nfc": (
        "這片PN532 V3：VCC→5V、GND共地；SCK18、MISO19、MOSI23、SS5（訊號仍3.3V）。僅模式表相符的V3板：斷電設1=OFF、2=ON；找不到開關時先核對版本。",
        "兩張卡輪流靠近各 20 次；無卡放 5 分鐘後再刷。",
        "有韌體版本；同一張卡 UID 固定、不同卡 UID 不同；等待後仍可刷。",
    ),
    "buzzer": (
        "25 經 1kΩ→NPN B；E→GND、C→蜂鳴器負端，S→3V3，中間腳不接。完整保護見 WIRING.md。",
        "不用輸入指令，聽音階、成功／錯誤提示和倒數嗶聲。",
        "聽得到不同音高；停止發聲的間隔安靜。必須實際聽，文字輸出不能代替。",
    ),
    "lock": (
        "26 經功率 MOSFET 控制 12V 鎖；必須有下拉與續流二極體，不能用 BSS138 推鎖。",
        "首次先不接 12V，確認 GPIO26 脈衝；斷電接鎖後輸入 u 加 Enter，每次間隔至少 2 秒。",
        "每次鎖舌縮回再復位，無重置誤動作；500ms 實際脈衝須量測，不以印出的文字認定。",
    ),
    "main": (
        "全部單體先通過，且 HX711 已 t/c/s 校正儲存；接線見 WIRING.md。",
        "連 SmartBasket / 密碼見 USB 序列埠，開 http://192.168.4.1；清空關蓋，記錄空籃基準。",
        "初始化全部 OK、感測 ready 且空籃；完成正常借還後，再按 HARDWARE_ACCEPTANCE.md 驗收。",
    ),
}


def show_guide(part):
    print(f"\n{LABELS[part]} [{part}]", flush=True)
    for label, value in zip(("接線", "操作", "正常結果"), GUIDES[part]):
        print(f"  {label}：{value}", flush=True)
    if part == "hx711":
        print("  電位轉換板本身不會回報電壓；3.3V/5V 須用萬用表量測。", flush=True)
    print("  上述結果仍需人工觀察；燒錄成功不代表硬體通過。\n", flush=True)


def choose(prompt, count):
    while True:
        value = input(prompt).strip()
        if value == "0":
            return None
        if value.isdecimal() and 1 <= int(value) <= count:
            return int(value) - 1
        print(f"請輸入 1～{count}，或 0 返回。")


def usb_ports(pio):
    result = subprocess.run([pio, "device", "list", "--json-output"], cwd=ROOT,
                            check=True, capture_output=True, text=True)
    devices = json.loads(result.stdout)
    ports = []
    for device in devices:
        port = device.get("port", "")
        hwid = device.get("hwid", "").upper()
        # Hide Bluetooth/debug ports. Unusual adapters remain available via --port.
        if "USB" in hwid or re.match(r"^(?:/dev/(?:cu\.(?:usb|wch)|tty(?:USB|ACM))|COM\d+$)", port):
            ports.append(device)
    return ports


def flash(pio, part, port, dry_run=False, monitor=True):
    print("改線前斷電；燒錄前關閉其他序列監看器。工具不會清除 NVS。", flush=True)
    if part == "lock":
        print("先不接 12V，確認控制脈衝後再斷電接鎖。工具不會自動發送開鎖指令。", flush=True)
    run([pio, "run", "-e", PARTS[part][0], "-t", "upload", "--upload-port", port], dry_run)
    if monitor:
        show_guide(part)  # Keep the instructions visible after the build output.
        print("監看：115200、Enter 送 LF；Ctrl+C 結束。韌體仍會運行，改線前要斷電。", flush=True)
        print("監看開啟後若沒看到開機訊息，按一下 ESP32 的 EN／RESET 鍵。", flush=True)
        run([pio, "device", "monitor", "--port", port, "--baud", "115200", "--echo", "--eol", "LF"], dry_run)


def menu(pio):
    parts = list(PARTS)
    print("智慧籃子元件測試：一次接好並測一種元件，電控鎖最後測。")
    print("目前板型設定：傳統 ESP32-WROOM-32，腳位依 WIRING.md。")
    while True:
        print("\n選擇測試元件（0 結束）：")
        for i, part in enumerate(parts, 1):
            print(f"  {i}. {LABELS[part]}")
        index = choose("輸入編號：", len(parts))
        if index is None:
            return 0
        part = parts[index]
        show_guide(part)
        try:
            ports = usb_ports(pio)
            if not ports:
                print("沒有找到 USB 序列埠。接好 USB 資料線後重新選元件；只有充電的線不能燒錄。")
                print("特殊序列埠可用 flash <元件> --port <實際埠> 指定。")
                continue
            print("選擇 ESP32 的 USB 埠；選定後開始燒錄（0 返回）：")
            for i, device in enumerate(ports, 1):
                print(f"  {i}. {device['port']}  {device.get('description', '')}")
            index = choose("輸入編號：", len(ports))
            if index is None:
                continue
            flash(pio, part, ports[index]["port"])
        except KeyboardInterrupt:
            print("\n已結束目前指令，返回選單。")
        except (subprocess.CalledProcessError, ValueError, OSError) as exc:
            print(f"測試工具遇到錯誤，已停止本次操作：{exc}")
        print("未自動判定通過；請對照上方正常結果。改線前先關外設電源，再拔 USB。")


def run(command, dry_run):
    print("+ " + shlex.join(command), flush=True)
    if not dry_run:
        subprocess.run(command, cwd=ROOT, check=True)


def main():
    parser = argparse.ArgumentParser(description="智慧籃子單體測試；接線見 WIRING.md。")
    commands = parser.add_subparsers(dest="action")
    commands.add_parser("list", help="列出測試項目與接線提示")
    commands.add_parser("ports", help="列出序列埠，不自動選擇")
    commands.add_parser("menu", help="中文選單：選元件、選 USB 埠、燒錄與監看（預設）")
    guide = commands.add_parser("guide", help="顯示某元件的接線、操作與正常結果，不需硬體")
    guide.add_argument("part", choices=PARTS)
    for name in ("build", "flash"):
        p = commands.add_parser(name, help="編譯" if name == "build" else "燒錄一支測試並開啟序列監看")
        p.add_argument("part", choices=PARTS)
        p.add_argument("--dry-run", action="store_true", help="只列命令，不執行")
        if name == "flash":
            p.add_argument("--port", required=True, help="實際序列埠，例如 /dev/cu.usbserial-0001 或 COM3")
            p.add_argument("--no-monitor", action="store_true", help="燒錄後不開啟監看")
    args = parser.parse_args()
    if args.action == "guide":
        show_guide(args.part)
        return 0
    if args.action == "list":
        for name, (env, note) in PARTS.items():
            print(f"{name:8} {env:15} {note}")
        print(f"完整接線：{ROOT / 'WIRING.md'}")
        print("全部實機項目仍須實測；編譯成功不代表硬體通過。")
        return 0

    dry_run = getattr(args, "dry_run", False)
    pio = shutil.which("pio") or shutil.which("platformio")
    if not pio:
        if not dry_run:
            parser.error("找不到 PlatformIO。先安裝 PlatformIO 並將 pio 加入 PATH。")
        pio = "pio"
    if args.action in (None, "menu"):
        return menu(pio)
    if args.action == "ports":
        run([pio, "device", "list"], False)
        return 0

    show_guide(args.part)
    if args.action == "flash":
        flash(pio, args.part, args.port, dry_run, not args.no_monitor)
    else:
        run([pio, "run", "-e", PARTS[args.part][0]], dry_run)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except subprocess.CalledProcessError as exc:
        print(f"命令失敗（exit {exc.returncode}），已停止；不繼續燒錄／監看。", file=sys.stderr)
        sys.exit(exc.returncode if exc.returncode > 0 else 1)
    except (KeyboardInterrupt, EOFError):
        print("\n已結束。")
        sys.exit(130)
