// main/kj_strings.h —— 限定猜拳界面上出现的全部文字（唯一允许出现非 ASCII 显示字面量的文件）。
//
// tools/gen_kj_fonts.py 从本文件提取字符集生成中文字体子集：
//   * 所有字符串字面量 + 可打印 ASCII → 正文字体 kj_zh14 / kj_zh18 / kj_zh26；
//   * KJ_BIG_ 开头的宏 → 大标题字体 kj_big48（只含这些字）；
//   * 牌面手势由 kj_hand36 / kj_hand64（Noto Emoji）提供，见 KJ_HAND_*。
// 改动任何文字后运行 `python3 tools/gen_kj_fonts.py generate ...` 重新生成字体，
// `python3 tools/gen_kj_fonts.py check`（已纳入 tools/validate.sh）会拦下缺字。
#pragma once

// ---- 大标题（kj_big48）----
#define KJ_BIG_TITLE_1      "限定"
#define KJ_BIG_TITLE_2      "猜拳"
#define KJ_BIG_CHALLENGE    "挑战！"
#define KJ_BIG_WIN          "胜"
#define KJ_BIG_LOSE         "负"
#define KJ_BIG_DRAW         "平"
#define KJ_BIG_CLEARED      "过关"
#define KJ_BIG_OUT          "出局"
#define KJ_BIG_FAILED       "失败"
#define KJ_BIG_VS           "VS"
#define KJ_BIG_BUMP         "碰！"
#define KJ_BIG_WELCOME      "欢迎"

// ---- 牌面手势（kj_hand36 / kj_hand64，Noto Emoji）----
#define KJ_HAND_ROCK        "\xE2\x9C\x8A"   // U+270A ✊
#define KJ_HAND_SCISSORS    "\xE2\x9C\x8C"   // U+270C ✌
#define KJ_HAND_PAPER       "\xE2\x9C\x8B"   // U+270B ✋

// ---- 通用 ----
#define KJ_STR_STAR         "★"
#define KJ_STR_STAR_EMPTY   "☆"
#define KJ_STR_ZAWA         "ざわ… ざわ…"
#define KJ_STR_CARD_ROCK    "石头"
#define KJ_STR_CARD_SCISSORS "剪刀"
#define KJ_STR_CARD_PAPER   "布"
#define KJ_STR_NO_FMT       "%02u号"          // 选手编号，如 07号
#define KJ_STR_BOT          "电脑"
#define KJ_STR_ME           "我"
#define KJ_STR_ROOM_FMT     "赌局 %s"
#define KJ_STR_COUNT_FMT    "×%u"
#define KJ_STR_BATTERY_NA   "--"

// ---- 首页 / 角色 ----
#define KJ_STR_SUBTITLE     "限定猜拳 · 多人对决"
#define KJ_STR_ROLE_PLAYER  "我是选手 · 加入赌局"
#define KJ_STR_ROLE_HOST    "我是庄家 · 开设赌局"
#define KJ_STR_ROLE_SETTINGS "设置 · 昵称与联机"
#define KJ_STR_HELLO_FMT    "你好，%s"
#define KJ_STR_NO_NAME      "还没有昵称 · 到设置里扫码登记"
#define KJ_STR_HINT_TITLE   "▲▼ 选择 · OK 确定"

// ---- 联网状态 ----
#define KJ_STR_NET_DIRECT   "直连 · 不需要电脑"
#define KJ_STR_NET_NO_RADIO "无线启动失败"
#define KJ_STR_NET_NO_WIFI  "未配置 Wi-Fi"
#define KJ_STR_NET_CONNECTING "正在连接 Wi-Fi"
#define KJ_STR_NET_SEARCHING "正在寻找电脑服务"
#define KJ_STR_NET_OK       "电脑服务已连接"
#define KJ_STR_NET_OLD      "电脑服务版本不同"

// ---- 设置 ----
#define KJ_STR_SET_TITLE    "设置"
#define KJ_STR_SET_NAME     "登记 / 修改昵称"
#define KJ_STR_SET_WIFI     "重新配网"
#define KJ_STR_SET_TO_HUB   "改用电脑服务联机"
#define KJ_STR_SET_TO_DIRECT "改用直连（免电脑）"
#define KJ_STR_SET_BACK     "返回"
#define KJ_STR_INFO_NAME    "昵称"
#define KJ_STR_INFO_WIFI    "Wi-Fi"
#define KJ_STR_INFO_HUB     "电脑"
#define KJ_STR_INFO_CONN    "联机"
#define KJ_STR_INFO_DIRECT  "直连（免电脑）"
#define KJ_STR_INFO_CHANNEL "信道"
#define KJ_STR_INFO_NO_NAME "未登记"
#define KJ_STR_INFO_NOT_FOUND "未找到"
#define KJ_STR_INFO_DEVICE_FMT "设备 %04X · 固件 %s"
#define KJ_STR_HINT_SETTINGS "▲▼ 选择 · OK 确定 · 长按 返回"
#define KJ_STR_CONN_Q_HUB   "改用电脑服务联机？"
#define KJ_STR_CONN_Q_DIRECT "改用直连（免电脑）？"
#define KJ_STR_CONN_SAME    "全场设备要用同一种方式"
#define KJ_STR_CONN_RESTART "确定后设备会重启"

// ---- 登记昵称 ----
#define KJ_STR_REG_TITLE    "登记昵称"
#define KJ_STR_REG_SCAN     "用微信或相机扫码，填写昵称"
#define KJ_STR_REG_SCAN2    "手机要连同一个 Wi-Fi"
#define KJ_STR_REG_OPENED   "已扫码，请在手机上填写"
#define KJ_STR_REG_DONE     "登记成功"
#define KJ_STR_REG_NO_HUB   "正在连接电脑服务…"
#define KJ_STR_REG_NO_HUB2  "连上后这里会出现二维码"
#define KJ_STR_HINT_REG     "OK 换二维码 · 长按 返回"
// 直连模式：先说明，再重启进入热点登记
#define KJ_STR_REG_AP_LEAD  "用手机给自己起个昵称"
#define KJ_STR_REG_AP_1     "设备会重启并开一个热点"
#define KJ_STR_REG_AP_2     "手机扫码连上，在网页里填写"
#define KJ_STR_REG_AP_3     "填好自动回来，不需要电脑"
#define KJ_STR_HINT_REG_AP  "OK 开始登记 · 长按 返回"

// ---- 配网 ----
#define KJ_STR_PROV_TITLE   "连接 Wi-Fi"
#define KJ_STR_PROV_STEP1   "1. 用手机相机扫码，连上热点"
#define KJ_STR_PROV_AP_FMT  "热点 %s"
#define KJ_STR_PROV_PASS_FMT "密码 %s"
#define KJ_STR_PROV_STEP2   "2. 在弹出的网页里选择 Wi-Fi"
#define KJ_STR_PROV_STEP2B  "没弹出就打开 192.168.4.1"
#define KJ_STR_PROV_PHONE_IN "手机已连上，请在网页里选择 Wi-Fi"
#define KJ_STR_PROV_TRYING_FMT "正在连接 %s…"
#define KJ_STR_PROV_OK      "连接成功，正在重启…"
#define KJ_STR_PROV_FAILED  "连接失败，请在手机上重试"
#define KJ_STR_HINT_PROV    "长按 先跳过"
// 直连模式的热点登记昵称（与配网共用热点页）
#define KJ_STR_NAP_STEP2    "2. 在弹出的网页里填写昵称"
#define KJ_STR_NAP_PHONE_IN "手机已连上，请在网页里填写昵称"
#define KJ_STR_NAP_DONE     "登记成功，正在回到游戏…"
#define KJ_STR_HINT_NAP     "长按 取消"

// ---- 找赌局 ----
#define KJ_STR_ROOMS_TITLE  "寻找赌局"
#define KJ_STR_ROOMS_SUB    "选择一个赌局入座"
#define KJ_STR_ROOMS_EMPTY  "正在搜索赌局"
#define KJ_STR_ROOMS_EMPTY2 "请先让庄家开设赌局"
#define KJ_STR_ROOMS_NO_HUB2 "确认电脑服务已启动、同一个 Wi-Fi"
#define KJ_STR_ROOMS_DIRECT2 "请庄家也用直连开设赌局"
#define KJ_STR_RADIO_RETRY  "请重启设备再试"
#define KJ_STR_ROOM_LINE_FMT "%s · %u 人"
#define KJ_STR_JOINING      "入座中…"
#define KJ_STR_HINT_ROOMS   "OK 入座 · 长按 返回"

// ---- 阶段 ----
#define KJ_STR_PHASE_LOBBY  "等待入座"
#define KJ_STR_PHASE_RUN    "赌局进行中"
#define KJ_STR_PHASE_ENDED  "赌局已结束"

// ---- 入座等待 ----
#define KJ_STR_YOUR_NO      "你的编号"
#define KJ_STR_SEATED_FMT   "已入座 %u 人"
#define KJ_STR_WAIT_START   "等待庄家开局"
#define KJ_STR_WAIT_NEXT    "本局已结束，等待下一局"
#define KJ_STR_HINT_SEAT    "长按 离座"

// ---- 手牌主页 ----
#define KJ_STR_STARS_LABEL  "星星"
#define KJ_STR_HAND_INFO_FMT "手牌 %u 张 · %u胜 %u负 %u平"
#define KJ_STR_FIND_OPP     "寻找对手"
#define KJ_STR_AVAIL_FMT    "可挑战 %u 人"
#define KJ_STR_HINT_HAND    "OK 选对手 · 长按 碰拳"

// ---- 碰拳 ----
#define KJ_STR_BUMP_WAIT    "等对方一起长按"
#define KJ_STR_BUMP_SUB     "两人面对面，同时长按 OK"
#define KJ_STR_HINT_BUMP    "正在配对…"
#define KJ_STR_MATCH_TITLE  "碰拳成功"
#define KJ_STR_MATCH_PEER   "你的对手"
#define KJ_STR_MATCH_FMT    "%u 秒后开打"
#define KJ_STR_HINT_MATCH   "长按 取消（认错人了）"

// ---- 选择对手 ----
#define KJ_STR_OPP_TITLE    "选择对手"
#define KJ_STR_OPP_SUB      "空闲的选手都在这里"
#define KJ_STR_OPP_EMPTY    "暂时没有可以挑战的人"
#define KJ_STR_OPP_EMPTY2   "对方可能正在对决"
#define KJ_STR_HINT_OPP     "OK 挑战 · 长按 返回"

// ---- 等待应战 ----
#define KJ_STR_WAIT_TITLE   "已发出挑战"
#define KJ_STR_WAIT_FMT     "等待应战 %u 秒"
#define KJ_STR_HINT_WAIT    "长按 撤回挑战"

// ---- 收到挑战 ----
#define KJ_STR_CHAL_FROM    "向你发起对决"
#define KJ_STR_CHAL_BOT     "电脑选手向你发起对决"
#define KJ_STR_CHAL_LEFT_FMT "%u 秒内应战"
#define KJ_STR_ACCEPT       "OK 应战"
#define KJ_STR_DECLINE      "长按 拒绝"

// ---- 出牌 ----
#define KJ_STR_DUEL_VS_FMT  "对决 vs %02u号"
#define KJ_STR_DUEL_VS_NAME_FMT "对决 vs %s"
#define KJ_STR_OPP_THINKING "对方思考中"
#define KJ_STR_OPP_LOCKED   "对方已出牌"
#define KJ_STR_PICK_PROMPT  "选一张牌，同时亮出"
#define KJ_STR_LOCKED_WAIT  "已出牌，等待亮牌"
#define KJ_STR_CARD_LEFT_FMT "%s · 剩 %u 张"
#define KJ_STR_HINT_CHOOSE  "▲▼ 选牌 · OK 出牌 · 长按 放弃"
#define KJ_STR_HINT_LOCKED  "暗牌已扣下，不能反悔"

// ---- 亮牌 ----
#define KJ_STR_STAR_PLUS    "+1 ★"
#define KJ_STR_STAR_MINUS   "-1 ★"
#define KJ_STR_STAR_SAME    "星星不变"
#define KJ_STR_HINT_REVEAL  "OK 继续"

// ---- 终局 ----
#define KJ_STR_CLEARED_FMT  "手牌出完，携 %u ★ 离场"
#define KJ_STR_OUT_SUB      "星星输光，被送往别室"
#define KJ_STR_FAIL_NOCARD  "手牌出完，星星不足 3 颗"
#define KJ_STR_FAIL_TIMEUP  "时间到，手牌没有出完"
#define KJ_STR_RECORD_FMT   "%u胜 %u负 %u平"
#define KJ_STR_FINAL_WAIT   "等待庄家宣布下一局"

// ---- 提示（toast）----
#define KJ_STR_T_DECLINED   "对方拒绝了挑战"
#define KJ_STR_T_CANCELLED  "对方撤回了挑战"
#define KJ_STR_T_TIMEOUT    "挑战超时，已作废"
#define KJ_STR_T_WITHDRAWN  "对方放弃了对决"
#define KJ_STR_T_ABORTED    "对方断线，对决作废"
#define KJ_STR_T_BUSY       "对方正忙，换个人吧"
#define KJ_STR_T_NOT_RUN    "赌局还没开始"
#define KJ_STR_T_NO_CARD    "这种牌已经用完了"
#define KJ_STR_T_INVALID    "现在不能这样操作"
#define KJ_STR_T_FULL       "赌局已满"
#define KJ_STR_T_KICKED     "你已离开这个赌局"
#define KJ_STR_T_NO_REPLY   "庄家没有回应，请稍后再试"
#define KJ_STR_T_LOST       "与庄家失联，重连中"
#define KJ_STR_T_RESTORED   "已恢复上次的赌局"
#define KJ_STR_T_NEED_TWO   "至少 2 人入座才能开局"
#define KJ_STR_T_DONE       "已执行"
#define KJ_STR_T_RADIO_FAIL "无线启动失败"
#define KJ_STR_T_BUMP_ALONE "没碰到对手，再试一次"
#define KJ_STR_T_BUMP_CROWD "同时碰拳的人太多，再试一次"
#define KJ_STR_T_MATCH_CANCEL "对方取消了碰拳"
#define KJ_STR_T_NAME_UPDATED "昵称已更新"
#define KJ_STR_T_NEED_HUB   "还没连上电脑服务"
#define KJ_STR_T_OLD_FW     "电脑服务版本不同，请更新固件"
#define KJ_STR_T_NO_WIFI    "请先在设置里配置 Wi-Fi"
#define KJ_STR_T_RESTARTING "正在重启…"

// ---- 庄家 ----
#define KJ_STR_HOST_TAG     "庄家"
#define KJ_STR_HOST_ROOM    "赌局号"
#define KJ_STR_STAT_SEATED  "入座"
#define KJ_STR_STAT_ONLINE  "在线"
#define KJ_STR_STAT_DUELS   "对决中"
#define KJ_STR_STAT_DONE    "已离场"
#define KJ_STR_M_START      "开始赌局"
#define KJ_STR_M_END        "结束赌局"
#define KJ_STR_M_NEW        "新一局"
#define KJ_STR_M_BOT_ADD    "添加电脑选手"
#define KJ_STR_M_BOT_DEL    "移除电脑选手"
#define KJ_STR_M_RESET      "清空所有选手"
#define KJ_STR_M_ROSTER     "选手名单"
#define KJ_STR_CONFIRM_FMT  "确定%s？"
#define KJ_STR_CONFIRM_HINT "OK 确定 · 长按 取消"
#define KJ_STR_BOARD_WIFI_FMT "看板已连电脑 %s"
#define KJ_STR_BOARD_USB    "看板已通过 USB 连接"
#define KJ_STR_BOARD_SEARCH "看板：正在寻找电脑服务"
#define KJ_STR_BOARD_NO_WIFI "看板：未配置 Wi-Fi"
#define KJ_STR_BOARD_DIRECT "直连 · USB 接电脑可看看板"

// ---- 庄家：选手名单 ----
#define KJ_STR_ROSTER_TITLE "选手名单"
#define KJ_STR_ROSTER_FMT   "%u 人"
#define KJ_STR_ROSTER_CARDS_FMT "%u 张"
#define KJ_STR_ROSTER_EMPTY "还没有人入座"
#define KJ_STR_OFFLINE      "离线"
#define KJ_STR_HINT_ROSTER  "▲▼ 翻看 · OK 返回"
#define KJ_STR_ST_WAITING   "等待"
#define KJ_STR_ST_IDLE      "空闲"
#define KJ_STR_ST_CHALLENGING "挑战中"
#define KJ_STR_ST_CHALLENGED "被挑战"
#define KJ_STR_ST_DUEL      "对决中"
#define KJ_STR_ST_CLEARED   "过关"
#define KJ_STR_ST_OUT       "出局"
#define KJ_STR_ST_FAILED    "失败"
#define KJ_STR_ST_BUMPING   "碰拳中"
#define KJ_STR_ST_MATCHED   "配对中"
#define KJ_STR_HINT_HOST    "▲▼ 选择 · OK 执行"
