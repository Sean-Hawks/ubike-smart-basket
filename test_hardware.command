#!/bin/zsh
# macOS: double-click in Finder to open the component test menu.
cd -- "${0:A:h}" || exit 1
python3 tools/bringup.py
test_status=$?
if (( test_status != 0 )); then
    print "測試工具已結束（狀態 $test_status）。按 Enter 關閉。"
    read -r
fi
exit "$test_status"
