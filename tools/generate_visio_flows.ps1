param(
    [string]$OutputPath = (Join-Path $PSScriptRoot '..\doc\TX_RX_STM_代码执行流程图.vsdx')
)

$ErrorActionPreference = 'Stop'

function Invoke-ComRetry {
    param([scriptblock]$Action, [ref]$Result, [int]$Attempts = 30)
    $Result.Value = $null
    for ($attempt = 1; $attempt -le $Attempts; $attempt++) {
        try {
            $Result.Value = & $Action
            return
        }
        catch {
            if ($attempt -eq $Attempts) {
                throw
            }
            Start-Sleep -Milliseconds 250
        }
    }
}

function Invoke-ComAction {
    param([scriptblock]$Action)
    $unused = $null
    Invoke-ComRetry $Action ([ref]$unused)
}

function Set-Cell {
    param($Shape, [string]$Cell, [string]$Formula)
    try { Invoke-ComAction { $Shape.CellsU($Cell).FormulaU = $Formula } } catch { }
}

function Add-Node {
    param(
        $Page,
        [hashtable]$Nodes,
        [string]$Id,
        [string]$Text,
        [double]$X,
        [double]$Y,
        [double]$W = 4.2,
        [double]$H = 0.72,
        [ValidateSet('start','process','decision','event','state','error','note')]
        [string]$Kind = 'process'
    )

    $shape = $null
    if ($Kind -eq 'start') {
        Invoke-ComRetry { $Page.DrawOval($X - $W / 2, $Y - $H / 2, $X + $W / 2, $Y + $H / 2) } ([ref]$shape)
    } else {
        Invoke-ComRetry { $Page.DrawRectangle($X - $W / 2, $Y - $H / 2, $X + $W / 2, $Y + $H / 2) } ([ref]$shape)
    }

    Invoke-ComAction { $shape.NameU = $Id }
    Invoke-ComAction { $shape.Text = $Text }
    Set-Cell $shape 'Char.Font' '4'
    Set-Cell $shape 'Char.Size' '8.5 pt'
    Set-Cell $shape 'Char.Style' '0'
    Set-Cell $shape 'Para.HorzAlign' '1'
    Set-Cell $shape 'VerticalAlign' '1'
    Set-Cell $shape 'LineWeight' '0.012 in'
    Set-Cell $shape 'Rounding' '0.08 in'
    Set-Cell $shape 'TxtMarginLeft' '0.08 in'
    Set-Cell $shape 'TxtMarginRight' '0.08 in'
    Set-Cell $shape 'TxtMarginTop' '0.04 in'
    Set-Cell $shape 'TxtMarginBottom' '0.04 in'

    switch ($Kind) {
        'start'    { $fill = 'RGB(226,239,218)'; $line = 'RGB(84,130,53)' }
        'process'  { $fill = 'RGB(221,235,247)'; $line = 'RGB(47,84,150)' }
        'decision' { $fill = 'RGB(255,242,204)'; $line = 'RGB(191,144,0)' }
        'event'    { $fill = 'RGB(234,209,220)'; $line = 'RGB(128,0,64)' }
        'state'    { $fill = 'RGB(226,239,218)'; $line = 'RGB(84,130,53)' }
        'error'    { $fill = 'RGB(244,204,204)'; $line = 'RGB(153,0,0)' }
        'note'     { $fill = 'RGB(242,242,242)'; $line = 'RGB(127,127,127)' }
    }
    Set-Cell $shape 'FillForegnd' $fill
    Set-Cell $shape 'LineColor' $line
    $Nodes[$Id] = $shape
    return $shape
}

function Add-Header {
    param($Page, [string]$Text, [double]$X, [double]$Y, [double]$W = 5.2)
    $shape = $null
    Invoke-ComRetry { $Page.DrawRectangle($X - $W / 2, $Y - 0.26, $X + $W / 2, $Y + 0.26) } ([ref]$shape)
    Invoke-ComAction { $shape.Text = $Text }
    Set-Cell $shape 'Char.Font' '4'
    Set-Cell $shape 'Char.Size' '11 pt'
    Set-Cell $shape 'Char.Style' '1'
    Set-Cell $shape 'Para.HorzAlign' '1'
    Set-Cell $shape 'VerticalAlign' '1'
    Set-Cell $shape 'FillForegnd' 'RGB(68,84,106)'
    Set-Cell $shape 'Char.Color' 'RGB(255,255,255)'
    Set-Cell $shape 'LineColor' 'RGB(68,84,106)'
    return $shape
}

function Add-Title {
    param($Page, [string]$Text, [double]$Width, [double]$Height)
    $shape = $null
    Invoke-ComRetry { $Page.DrawRectangle(0.5, $Height - 0.95, $Width - 0.5, $Height - 0.25) } ([ref]$shape)
    Invoke-ComAction { $shape.Text = $Text }
    Set-Cell $shape 'Char.Font' '4'
    Set-Cell $shape 'Char.Size' '17 pt'
    Set-Cell $shape 'Char.Style' '1'
    Set-Cell $shape 'Para.HorzAlign' '1'
    Set-Cell $shape 'VerticalAlign' '1'
    Set-Cell $shape 'FillPattern' '0'
    Set-Cell $shape 'LinePattern' '0'
}

function Add-Edge {
    param(
        $Page,
        [hashtable]$Nodes,
        [string]$From,
        [string]$To,
        [string]$Label = ''
    )
    $connector = $null
    Invoke-ComRetry { $Page.Drop($script:VisioApp.ConnectorToolDataObject, 0, 0) } ([ref]$connector)
    Invoke-ComAction { $connector.CellsU('BeginX').GlueTo($Nodes[$From].CellsU('PinX')) }
    Invoke-ComAction { $connector.CellsU('EndX').GlueTo($Nodes[$To].CellsU('PinX')) }
    Set-Cell $connector 'LineColor' 'RGB(89,89,89)'
    Set-Cell $connector 'LineWeight' '0.010 in'
    Set-Cell $connector 'EndArrow' '4'
    Set-Cell $connector 'ConLineRouteExt' '1'
    if ($Label) {
        Invoke-ComAction { $connector.Text = $Label }
        Set-Cell $connector 'Char.Font' '4'
        Set-Cell $connector 'Char.Size' '7.5 pt'
        Set-Cell $connector 'Para.HorzAlign' '1'
    }
    return $connector
}

function Configure-Page {
    param($Page, [string]$Name, [double]$Width, [double]$Height)
    Invoke-ComAction { $Page.Name = $Name }
    Invoke-ComAction { $Page.PageSheet.CellsU('PageWidth').ResultIU = $Width }
    Invoke-ComAction { $Page.PageSheet.CellsU('PageHeight').ResultIU = $Height }
    Set-Cell $Page.PageSheet 'RouteStyle' '16'
    Add-Title $Page "$Name（按当前源码执行顺序）" $Width $Height
}

function Build-TxPage {
    param($Page)
    $nodes = @{}
    Configure-Page $Page 'TX发送端' 19 25
    Add-Header $Page '启动与配置窗口' 3.25 23.35 5.5 | Out-Null
    Add-Header $Page 'application_run() 主循环' 9.55 23.35 5.8 | Out-Null
    Add-Header $Page '图片发送与中断事件' 16.0 23.35 5.4 | Out-Null

    Add-Node $Page $nodes 'tx_start' '上电 → main()' 3.25 22.25 3.5 0.7 start | Out-Null
    Add-Node $Page $nodes 'tx_init' "application_init()\n1 时钟  2 日志  3 SN\n4 看门狗  5 LED\n6 CX93510/OV7676并休眠\n7 SPIM挂起  8 ADXL362\n9 Radio接收  10 TIMER1" 3.25 20.55 5.25 2.15 process | Out-Null
    Add-Node $Page $nodes 'tx_cfg_begin' 'sn_config_window_run()\n打开0x40～0x46配置窗口' 3.25 18.75 4.7 0.8 process | Out-Null
    Add-Node $Page $nodes 'tx_cfg_done' '配置完成？' 3.25 17.45 3.0 0.68 decision | Out-Null
    Add-Node $Page $nodes 'tx_cfg_rx' 'radio_rx_process()\n处理一个接收包' 3.25 16.15 4.1 0.78 process | Out-Null
    Add-Node $Page $nodes 'tx_cfg_timeout' '配置已完成，或连续无合法命令达到窗口时间？' 3.25 14.75 4.9 0.86 decision | Out-Null
    Add-Node $Page $nodes 'tx_cfg_wait' '必要时翻转LED\n日志 → 喂狗 → __WFE()' 3.25 13.30 4.4 0.86 process | Out-Null
    Add-Node $Page $nodes 'tx_cfg_end' '关闭配置窗口和LED' 3.25 11.95 3.9 0.72 process | Out-Null
    Add-Node $Page $nodes 'tx_idle' 'radio_enter_idle()' 3.25 10.75 3.4 0.68 process | Out-Null
    Add-Node $Page $nodes 'tx_burst' '当前SN连续广播3次' 3.25 9.55 3.8 0.68 process | Out-Null
    Add-Node $Page $nodes 'tx_rtc' '启动RTC2低功耗调度器' 3.25 8.35 4.1 0.68 process | Out-Null
    Add-Node $Page $nodes 'tx_first' 'g_capture_due = true\n首帧立即采集' 3.25 7.05 3.8 0.78 state | Out-Null

    Add-Edge $Page $nodes tx_start tx_init | Out-Null
    Add-Edge $Page $nodes tx_init tx_cfg_begin | Out-Null
    Add-Edge $Page $nodes tx_cfg_begin tx_cfg_done | Out-Null
    Add-Edge $Page $nodes tx_cfg_done tx_cfg_rx '否' | Out-Null
    Add-Edge $Page $nodes tx_cfg_rx tx_cfg_timeout | Out-Null
    Add-Edge $Page $nodes tx_cfg_timeout tx_cfg_wait '否' | Out-Null
    Add-Edge $Page $nodes tx_cfg_wait tx_cfg_done | Out-Null
    Add-Edge $Page $nodes tx_cfg_done tx_cfg_end '是' | Out-Null
    Add-Edge $Page $nodes tx_cfg_timeout tx_cfg_end '是' | Out-Null
    Add-Edge $Page $nodes tx_cfg_end tx_idle | Out-Null
    Add-Edge $Page $nodes tx_idle tx_burst | Out-Null
    Add-Edge $Page $nodes tx_burst tx_rtc | Out-Null
    Add-Edge $Page $nodes tx_rtc tx_first | Out-Null

    Add-Node $Page $nodes 'tx_loop' '进入一轮 application_run()' 9.55 22.25 4.5 0.7 start | Out-Null
    Add-Node $Page $nodes 'tx_busy' 'image_runtime_busy 且活动时钟关闭？' 9.55 21.05 4.7 0.72 decision | Out-Null
    Add-Node $Page $nodes 'tx_clk_on' 'active_clock_start()' 12.15 19.95 3.5 0.66 process | Out-Null
    Add-Node $Page $nodes 'tx_radio' '1  radio_rx_process()' 9.55 18.85 4.1 0.64 process | Out-Null
    Add-Node $Page $nodes 'tx_ack' '2  image_ack_service()' 9.55 17.75 4.1 0.64 process | Out-Null
    Add-Node $Page $nodes 'tx_fast' '3  image_fast_scan_service()' 9.55 16.65 4.4 0.64 process | Out-Null
    Add-Node $Page $nodes 'tx_sn' '4  capsule_sn_broadcast_service()' 9.55 15.55 4.7 0.64 process | Out-Null
    Add-Node $Page $nodes 'tx_capture' '5  image_capture_task()' 9.55 14.45 4.1 0.64 process | Out-Null
    Add-Node $Page $nodes 'tx_send' '6  image_tx_service()\n每轮最多一个DATA分片' 9.55 13.25 4.4 0.78 process | Out-Null
    Add-Node $Page $nodes 'tx_log' '7  TX_LOG_PROCESS()' 9.55 12.05 3.9 0.64 process | Out-Null
    Add-Node $Page $nodes 'tx_wdt' '8  watchdog_feed()' 9.55 10.95 3.9 0.64 process | Out-Null
    Add-Node $Page $nodes 'tx_idle_check' '活动时钟开启且当前无任务？' 9.55 9.75 4.4 0.72 decision | Out-Null
    Add-Node $Page $nodes 'tx_clk_off' 'active_clock_stop()' 12.1 8.65 3.6 0.66 process | Out-Null
    Add-Node $Page $nodes 'tx_wfe' '9  __WFE() 等待中断' 9.55 7.55 4.1 0.68 event | Out-Null

    Add-Edge $Page $nodes tx_first tx_loop | Out-Null
    Add-Edge $Page $nodes tx_loop tx_busy | Out-Null
    Add-Edge $Page $nodes tx_busy tx_clk_on '是' | Out-Null
    Add-Edge $Page $nodes tx_busy tx_radio '否' | Out-Null
    Add-Edge $Page $nodes tx_clk_on tx_radio | Out-Null
    Add-Edge $Page $nodes tx_radio tx_ack | Out-Null
    Add-Edge $Page $nodes tx_ack tx_fast | Out-Null
    Add-Edge $Page $nodes tx_fast tx_sn | Out-Null
    Add-Edge $Page $nodes tx_sn tx_capture | Out-Null
    Add-Edge $Page $nodes tx_capture tx_send | Out-Null
    Add-Edge $Page $nodes tx_send tx_log | Out-Null
    Add-Edge $Page $nodes tx_log tx_wdt | Out-Null
    Add-Edge $Page $nodes tx_wdt tx_idle_check | Out-Null
    Add-Edge $Page $nodes tx_idle_check tx_clk_off '是' | Out-Null
    Add-Edge $Page $nodes tx_idle_check tx_wfe '否' | Out-Null
    Add-Edge $Page $nodes tx_clk_off tx_wfe | Out-Null
    Add-Edge $Page $nodes tx_wfe tx_loop '下一轮' | Out-Null

    Add-Node $Page $nodes 'tx_cap_due' 'image_capture_task()\n到期→唤醒摄像头/开灯/启动ADXL362' 16.0 22.15 5.0 0.88 process | Out-Null
    Add-Node $Page $nodes 'tx_warm' '等待25ms预热截止' 16.0 20.85 4.0 0.66 event | Out-Null
    Add-Node $Page $nodes 'tx_grab' '读取加速度 → 抓取JPEG\n关灯 → OV7676休眠' 16.0 19.55 4.8 0.86 process | Out-Null
    Add-Node $Page $nodes 'tx_valid' 'JPEG长度是否合法？' 16.0 18.25 3.8 0.68 decision | Out-Null
    Add-Node $Page $nodes 'tx_active' '设置帧ID、分片数\nactive=true' 16.0 17.05 4.0 0.76 state | Out-Null
    Add-Node $Page $nodes 'tx_begin' '首轮第0片：BEGIN发送2次' 16.0 15.75 4.3 0.7 process | Out-Null
    Add-Node $Page $nodes 'tx_data' '读取本片JPEG → 累加校验和\n发送一个DATA' 16.0 14.45 4.8 0.84 process | Out-Null
    Add-Node $Page $nodes 'tx_all' '全部分片完成？' 16.0 13.15 3.6 0.68 decision | Out-Null
    Add-Node $Page $nodes 'tx_end' '发送END并携带整图校验和' 16.0 11.95 4.3 0.7 process | Out-Null
    Add-Node $Page $nodes 'tx_ack_wait' '首轮：打开RX并等待ACK' 16.0 10.75 4.0 0.68 state | Out-Null
    Add-Node $Page $nodes 'tx_ack_ok' '收到匹配0x10？' 16.0 9.55 3.6 0.68 decision | Out-Null
    Add-Node $Page $nodes 'tx_finish' '完成本帧并关闭Radio RX' 16.0 8.35 4.0 0.68 state | Out-Null
    Add-Node $Page $nodes 'tx_timeout' 'ACK超时且未达到重发上限？' 16.0 7.15 4.5 0.72 decision | Out-Null
    Add-Node $Page $nodes 'tx_retry' '重置分片计数\n重发DATA+END，不重发BEGIN' 16.0 5.85 4.7 0.82 process | Out-Null
    Add-Node $Page $nodes 'tx_scan' '收到匹配0x12：进入快速扫描\n每8ms发0x05，前120ms插入0x11' 16.0 4.35 5.0 0.96 state | Out-Null

    Add-Edge $Page $nodes tx_cap_due tx_warm | Out-Null
    Add-Edge $Page $nodes tx_warm tx_grab | Out-Null
    Add-Edge $Page $nodes tx_grab tx_valid | Out-Null
    Add-Edge $Page $nodes tx_valid tx_active '是' | Out-Null
    Add-Edge $Page $nodes tx_active tx_begin | Out-Null
    Add-Edge $Page $nodes tx_begin tx_data | Out-Null
    Add-Edge $Page $nodes tx_data tx_all | Out-Null
    Add-Edge $Page $nodes tx_all tx_data '否：下一轮' | Out-Null
    Add-Edge $Page $nodes tx_all tx_end '是' | Out-Null
    Add-Edge $Page $nodes tx_end tx_ack_wait | Out-Null
    Add-Edge $Page $nodes tx_ack_wait tx_ack_ok | Out-Null
    Add-Edge $Page $nodes tx_ack_ok tx_finish '0x10' | Out-Null
    Add-Edge $Page $nodes tx_ack_ok tx_scan '0x12' | Out-Null
    Add-Edge $Page $nodes tx_ack_ok tx_timeout '未收到' | Out-Null
    Add-Edge $Page $nodes tx_timeout tx_retry '是' | Out-Null
    Add-Edge $Page $nodes tx_retry tx_data | Out-Null
    Add-Edge $Page $nodes tx_timeout tx_finish '否：放弃' | Out-Null

    Add-Node $Page $nodes 'tx_irq_note' "中断只产生事件：\nTIMER1：g_time_ms++\nRTC2：SN广播到期、采集到期\nRADIO：CRC正确包写入接收队列" 3.25 4.25 5.1 1.45 note | Out-Null
}

function Build-RxPage {
    param($Page)
    $nodes = @{}
    Configure-Page $Page 'RX接收端' 19 27
    Add-Header $Page '初始化与主循环' 3.15 25.35 5.3 | Out-Null
    Add-Header $Page 'Radio与图片数据路径' 9.55 25.35 5.6 | Out-Null
    Add-Header $Page '天线状态机与UART' 16.0 25.35 5.3 | Out-Null

    Add-Node $Page $nodes 'rx_start' '上电 → main()' 3.15 24.25 3.5 0.7 start | Out-Null
    Add-Node $Page $nodes 'rx_platform' "receiver_platform_init()\n时钟 → 模式脚 → RF1662默认天线" 3.15 22.85 5.0 0.86 process | Out-Null
    Add-Node $Page $nodes 'rx_service_init' "receiver_service_init()\n日志 → 绑定状态 → 图片状态 → TIMER1" 3.15 21.45 5.0 0.86 process | Out-Null
    Add-Node $Page $nodes 'rx_comm' "receiver_comm_init()\nUART空闲捕获 → UART → Radio → 天线管理器" 3.15 20.05 5.1 0.9 process | Out-Null
    Add-Node $Page $nodes 'rx_loop' '进入一轮 run_once()' 3.15 18.65 4.0 0.68 start | Out-Null
    Add-Node $Page $nodes 'rx_ev1' '1 antenna_service_events()' 3.15 17.55 4.3 0.62 process | Out-Null
    Add-Node $Page $nodes 'rx_radio_one' '2 radio_process_one()' 3.15 16.50 4.0 0.62 process | Out-Null
    Add-Node $Page $nodes 'rx_uart_rx' '3 uart_rx_service()' 3.15 15.45 4.0 0.62 process | Out-Null
    Add-Node $Page $nodes 'rx_uart_ctrl' '4 uart_control_service()' 3.15 14.40 4.2 0.62 process | Out-Null
    Add-Node $Page $nodes 'rx_uart_tx' '5 uart_tx_service()' 3.15 13.35 4.0 0.62 process | Out-Null
    Add-Node $Page $nodes 'rx_ev2' '6 antenna_service_events()\n处理本轮新事件' 3.15 12.20 4.4 0.76 process | Out-Null
    Add-Node $Page $nodes 'rx_sched' '7 antenna_service_schedule()' 3.15 11.05 4.6 0.64 process | Out-Null
    Add-Node $Page $nodes 'rx_log' 'NRF_LOG_PROCESS()' 3.15 9.95 3.8 0.62 process | Out-Null
    Add-Node $Page $nodes 'rx_sleep_q' '本轮无业务且无日志？' 3.15 8.85 3.9 0.68 decision | Out-Null
    Add-Node $Page $nodes 'rx_wfe' '__WFE() 等待中断' 3.15 7.70 3.8 0.66 event | Out-Null

    foreach ($pair in @(@('rx_start','rx_platform'),@('rx_platform','rx_service_init'),@('rx_service_init','rx_comm'),@('rx_comm','rx_loop'),@('rx_loop','rx_ev1'),@('rx_ev1','rx_radio_one'),@('rx_radio_one','rx_uart_rx'),@('rx_uart_rx','rx_uart_ctrl'),@('rx_uart_ctrl','rx_uart_tx'),@('rx_uart_tx','rx_ev2'),@('rx_ev2','rx_sched'),@('rx_sched','rx_log'),@('rx_log','rx_sleep_q'))) { Add-Edge $Page $nodes $pair[0] $pair[1] | Out-Null }
    Add-Edge $Page $nodes rx_sleep_q rx_wfe '是' | Out-Null
    Add-Edge $Page $nodes rx_sleep_q rx_loop '否' | Out-Null
    Add-Edge $Page $nodes rx_wfe rx_loop '中断唤醒' | Out-Null

    Add-Node $Page $nodes 'rr_irq' 'RADIO_IRQHandler()\nEND事件，读取CRC与RSSI' 9.55 24.20 4.8 0.82 event | Out-Null
    Add-Node $Page $nodes 'rr_state' 'antenna_on_radio_packet()\n当前是SEEK_END或FAST_SCAN？' 9.55 22.80 5.0 0.84 decision | Out-Null
    Add-Node $Page $nodes 'rr_seek' 'SEEK_END：匹配END或0x11\n只挂起事件，不进入图片队列' 12.1 21.40 4.8 0.86 state | Out-Null
    Add-Node $Page $nodes 'rr_scan' 'FAST_SCAN：统计0x05数量和RSSI\n不进入图片队列' 12.1 20.10 4.8 0.84 state | Out-Null
    Add-Node $Page $nodes 'rr_queue_q' 'CRC正确且该包需要入队？' 9.55 20.75 4.4 0.7 decision | Out-Null
    Add-Node $Page $nodes 'rr_queue' '复制到Radio环形队列' 9.55 19.45 4.1 0.66 process | Out-Null
    Add-Node $Page $nodes 'rr_arm' 'receiver_radio_arm()' 9.55 18.30 3.8 0.62 process | Out-Null

    Add-Edge $Page $nodes rr_irq rr_state | Out-Null
    Add-Edge $Page $nodes rr_state rr_seek 'SEEK_END' | Out-Null
    Add-Edge $Page $nodes rr_state rr_scan 'FAST_SCAN' | Out-Null
    Add-Edge $Page $nodes rr_state rr_queue_q '其他状态' | Out-Null
    Add-Edge $Page $nodes rr_queue_q rr_queue '是' | Out-Null
    Add-Edge $Page $nodes rr_queue_q rr_arm '否' | Out-Null
    Add-Edge $Page $nodes rr_queue rr_arm | Out-Null
    Add-Edge $Page $nodes rr_seek rr_arm | Out-Null
    Add-Edge $Page $nodes rr_scan rr_arm | Out-Null

    Add-Node $Page $nodes 'rp_take' '主循环取一个Radio队列包' 9.55 16.95 4.3 0.68 process | Out-Null
    Add-Node $Page $nodes 'rp_ctrl' 'control_handle_radio_packet()' 9.55 15.75 4.5 0.66 process | Out-Null
    Add-Node $Page $nodes 'rp_ctrl_q' '是否已作为控制包处理？' 9.55 14.55 4.1 0.68 decision | Out-Null
    Add-Node $Page $nodes 'rp_image' 'image_process_packet()' 9.55 13.35 4.0 0.66 process | Out-Null
    Add-Node $Page $nodes 'rp_type' 'BEGIN / DATA / END？\n并且SN匹配绑定？' 9.55 12.10 4.2 0.76 decision | Out-Null
    Add-Node $Page $nodes 'rp_begin' 'BEGIN：校验长度和分片数\n建立图片上下文' 12.1 10.75 4.6 0.8 process | Out-Null
    Add-Node $Page $nodes 'rp_data' 'DATA：过滤越界/重复\n按索引复制；最后片只复制剩余长度' 9.55 10.35 5.0 0.92 process | Out-Null
    Add-Node $Page $nodes 'rp_end' 'END：检查分片完整性\n计算整图8位校验和' 7.1 10.75 4.4 0.82 process | Out-Null
    Add-Node $Page $nodes 'rp_valid' '完整且校验正确？' 9.55 8.85 3.8 0.68 decision | Out-Null
    Add-Node $Page $nodes 'rp_fail' '失败计数或校验失败\n必要时触发换天线' 6.95 7.55 4.2 0.8 error | Out-Null
    Add-Node $Page $nodes 'rp_ack' '优先尝试0x12扫描请求\n否则发送0x10 ACK两次' 9.55 7.45 4.5 0.84 process | Out-Null
    Add-Node $Page $nodes 'rp_info' '刷新链路状态 → 构建设备信息\nqueue_image()排队发STM' 9.55 6.05 4.8 0.86 state | Out-Null

    Add-Edge $Page $nodes rx_radio_one rp_take | Out-Null
    Add-Edge $Page $nodes rp_take rp_ctrl | Out-Null
    Add-Edge $Page $nodes rp_ctrl rp_ctrl_q | Out-Null
    Add-Edge $Page $nodes rp_ctrl_q rp_image '否' | Out-Null
    Add-Edge $Page $nodes rp_ctrl_q rr_arm '是：出队' | Out-Null
    Add-Edge $Page $nodes rp_image rp_type | Out-Null
    Add-Edge $Page $nodes rp_type rp_begin 'BEGIN' | Out-Null
    Add-Edge $Page $nodes rp_type rp_data 'DATA' | Out-Null
    Add-Edge $Page $nodes rp_type rp_end 'END' | Out-Null
    Add-Edge $Page $nodes rp_end rp_valid | Out-Null
    Add-Edge $Page $nodes rp_valid rp_fail '否' | Out-Null
    Add-Edge $Page $nodes rp_valid rp_ack '是' | Out-Null
    Add-Edge $Page $nodes rp_ack rp_info | Out-Null

    Add-Node $Page $nodes 'ra_disc' 'DISCOVERY\n未绑定：按驻留时间轮询12路' 16.0 24.15 4.5 0.82 state | Out-Null
    Add-Node $Page $nodes 'ra_bound' '绑定成功？' 16.0 22.85 3.3 0.66 decision | Out-Null
    Add-Node $Page $nodes 'ra_seek' 'SEEK_END\n8ms逐路寻找绑定胶囊END' 16.0 21.55 4.5 0.8 state | Out-Null
    Add-Node $Page $nodes 'ra_end' '收到END：最多发送两次0x12\n当前天线保持150ms' 16.0 20.15 4.8 0.86 process | Out-Null
    Add-Node $Page $nodes 'ra_11' '收到匹配0x11？' 16.0 18.85 3.6 0.68 decision | Out-Null
    Add-Node $Page $nodes 'ra_fast' 'FAST_SCAN\n逐路累计有效0x05数量与RSSI' 16.0 17.45 4.7 0.84 state | Out-Null
    Add-Node $Page $nodes 'ra_candidates' '扫描完成：构建最多3路候选' 16.0 16.10 4.5 0.7 process | Out-Null
    Add-Node $Page $nodes 'ra_have' '存在有效候选？' 16.0 14.90 3.5 0.66 decision | Out-Null
    Add-Node $Page $nodes 'ra_locked' 'LOCKED\n锁定第一候选天线' 16.0 13.65 4.0 0.76 state | Out-Null
    Add-Node $Page $nodes 'ra_weak' '目标包超时 / 完整图超时\n或连续失败达到限制？' 16.0 12.30 4.6 0.82 decision | Out-Null
    Add-Node $Page $nodes 'ra_next' '还有下一候选？' 16.0 10.95 3.6 0.66 decision | Out-Null
    Add-Node $Page $nodes 'ra_lock_next' '锁定下一候选' 13.9 9.75 3.4 0.64 state | Out-Null
    Add-Node $Page $nodes 'ra_reseek' '候选耗尽或无有效候选\n重新进入SEEK_END' 17.4 9.60 3.8 0.82 state | Out-Null

    Add-Edge $Page $nodes ra_disc ra_bound | Out-Null
    Add-Edge $Page $nodes ra_bound ra_disc '否：继续轮询' | Out-Null
    Add-Edge $Page $nodes ra_bound ra_seek '是' | Out-Null
    Add-Edge $Page $nodes ra_seek ra_end '匹配END' | Out-Null
    Add-Edge $Page $nodes ra_end ra_11 | Out-Null
    Add-Edge $Page $nodes ra_11 ra_fast '是' | Out-Null
    Add-Edge $Page $nodes ra_11 ra_seek '否' | Out-Null
    Add-Edge $Page $nodes ra_fast ra_candidates | Out-Null
    Add-Edge $Page $nodes ra_candidates ra_have | Out-Null
    Add-Edge $Page $nodes ra_have ra_locked '是' | Out-Null
    Add-Edge $Page $nodes ra_have ra_reseek '否' | Out-Null
    Add-Edge $Page $nodes ra_locked ra_weak | Out-Null
    Add-Edge $Page $nodes ra_weak ra_locked '否' | Out-Null
    Add-Edge $Page $nodes ra_weak ra_next '是' | Out-Null
    Add-Edge $Page $nodes ra_next ra_lock_next '是' | Out-Null
    Add-Edge $Page $nodes ra_next ra_reseek '否' | Out-Null
    Add-Edge $Page $nodes ra_lock_next ra_weak | Out-Null
    Add-Edge $Page $nodes ra_reseek ra_seek | Out-Null

    Add-Node $Page $nodes 'ru_queue' 'UART图片队列：\nFF 55 12 34 81 + DeviceInfo + JPEG + checksum' 16.0 7.70 5.2 1.0 process | Out-Null
    Add-Node $Page $nodes 'ru_dma' 'uart_tx_service()\n每轮启动一个DMA块' 16.0 6.30 4.3 0.78 process | Out-Null
    Add-Node $Page $nodes 'ru_irq' 'APP_UART_TX_EMPTY中断\n只设置完成标志' 16.0 5.05 4.3 0.78 event | Out-Null
    Add-Node $Page $nodes 'ru_more' '还有未发送数据？' 16.0 3.85 3.6 0.66 decision | Out-Null
    Add-Node $Page $nodes 'ru_done' '释放整帧UART事务\n随后可补发pending_control' 16.0 2.55 4.5 0.84 state | Out-Null
    Add-Edge $Page $nodes rp_info ru_queue | Out-Null
    Add-Edge $Page $nodes ru_queue ru_dma | Out-Null
    Add-Edge $Page $nodes ru_dma ru_irq | Out-Null
    Add-Edge $Page $nodes ru_irq ru_more | Out-Null
    Add-Edge $Page $nodes ru_more ru_dma '是' | Out-Null
    Add-Edge $Page $nodes ru_more ru_done '否' | Out-Null

    Add-Node $Page $nodes 'rx_irq_note' "TIMER1每1ms：\n更新统一时间 → 产生天线service_due\n→ 检查UART 100ms静默分包\nUART接收中断只搬字节到ring" 3.15 4.75 5.1 1.35 note | Out-Null
}

function Build-StmPage {
    param($Page)
    $nodes = @{}
    Configure-Page $Page 'STM32主控' 19 27
    Add-Header $Page '启动与主循环' 3.15 25.35 5.3 | Out-Null
    Add-Header $Page 'USART2：RX图片输入' 9.55 25.35 5.5 | Out-Null
    Add-Header $Page 'SD存储、PC转发与USART3' 16.0 25.35 5.4 | Out-Null

    Add-Node $Page $nodes 'sm_start' '上电 → main()' 3.15 24.25 3.5 0.7 start | Out-Null
    Add-Node $Page $nodes 'sm_hal' 'HAL_Init()' 3.15 23.10 3.4 0.62 process | Out-Null
    Add-Node $Page $nodes 'sm_clock' 'SystemClock_Config()\nHSE 8MHz × PLL9 = 72MHz' 3.15 21.90 4.5 0.78 process | Out-Null
    Add-Node $Page $nodes 'sm_mx' "按顺序初始化：\nGPIO → DMA → IWDG → TIM1\nUSART1 → USART2 → USART3\nSDIO → FATFS" 3.15 20.20 5.0 1.35 process | Out-Null
    Add-Node $Page $nodes 'sm_log' 'AppLog_Init()' 3.15 18.95 3.5 0.62 process | Out-Null
    Add-Node $Page $nodes 'sm_uart_init' 'AppUartRx_Init()\n协议初始化 + USART2/3循环DMA' 3.15 17.65 4.8 0.82 process | Out-Null
    Add-Node $Page $nodes 'sm_uart_ok' '双UART DMA初始化成功？' 3.15 16.35 4.1 0.68 decision | Out-Null
    Add-Node $Page $nodes 'sm_tim' '启动TIM1 1ms中断' 3.15 15.10 3.8 0.64 process | Out-Null
    Add-Node $Page $nodes 'sm_sd' 'SdStorage_Init()\n挂载FATFS；失败只告警' 3.15 13.85 4.3 0.78 process | Out-Null
    Add-Node $Page $nodes 'sm_loop' '进入while(1)' 3.15 12.60 3.4 0.64 start | Out-Null
    Add-Node $Page $nodes 'sm_process' 'AppUartRx_Process()' 3.15 11.45 3.9 0.64 process | Out-Null
    Add-Node $Page $nodes 'sm_u2' '1 Consume USART2 ring' 3.15 10.35 3.9 0.62 process | Out-Null
    Add-Node $Page $nodes 'sm_u3' '2 Consume USART3 ring' 3.15 9.25 3.9 0.62 process | Out-Null
    Add-Node $Page $nodes 'sm_proto' '3 CapsuleProtocol_Process()' 3.15 8.15 4.2 0.62 process | Out-Null
    Add-Node $Page $nodes 'sm_feed' 'HAL_IWDG_Refresh()' 3.15 7.05 3.8 0.62 process | Out-Null
    Add-Node $Page $nodes 'sm_error' 'Error_Handler()\n关闭中断，等待IWDG复位' 5.65 15.15 4.1 0.8 error | Out-Null

    foreach ($pair in @(@('sm_start','sm_hal'),@('sm_hal','sm_clock'),@('sm_clock','sm_mx'),@('sm_mx','sm_log'),@('sm_log','sm_uart_init'),@('sm_uart_init','sm_uart_ok'))) { Add-Edge $Page $nodes $pair[0] $pair[1] | Out-Null }
    Add-Edge $Page $nodes sm_uart_ok sm_error '否' | Out-Null
    Add-Edge $Page $nodes sm_uart_ok sm_tim '是' | Out-Null
    foreach ($pair in @(@('sm_tim','sm_sd'),@('sm_sd','sm_loop'),@('sm_loop','sm_process'),@('sm_process','sm_u2'),@('sm_u2','sm_u3'),@('sm_u3','sm_proto'),@('sm_proto','sm_feed'))) { Add-Edge $Page $nodes $pair[0] $pair[1] | Out-Null }
    Add-Edge $Page $nodes sm_feed sm_loop '下一轮' | Out-Null

    Add-Node $Page $nodes 'su_irq' 'USART2 IDLE / DMA半满 / DMA全满' 9.55 24.20 4.9 0.7 event | Out-Null
    Add-Node $Page $nodes 'su_commit' 'HAL_UARTEx_RxEventCallback()\nCommitDmaEvent()' 9.55 22.95 4.5 0.8 process | Out-Null
    Add-Node $Page $nodes 'su_wrap' 'DMA位置发生回绕？' 9.55 21.70 3.8 0.66 decision | Out-Null
    Add-Node $Page $nodes 'su_linear' '复制[last,current)' 7.55 20.50 3.5 0.62 process | Out-Null
    Add-Node $Page $nodes 'su_two' '复制[last,end)\n再复制[0,current)' 11.55 20.50 3.8 0.76 process | Out-Null
    Add-Node $Page $nodes 'su_ring' '写入USART2软件RingBuffer' 9.55 19.20 4.3 0.66 state | Out-Null
    Add-Node $Page $nodes 'su_input' '主循环：InputFromNrf()' 9.55 17.90 4.1 0.66 process | Out-Null
    Add-Node $Page $nodes 'su_ctrl' '在PREAMBLE状态识别到ZAYS控制应答？' 9.55 16.60 4.8 0.72 decision | Out-Null
    Add-Node $Page $nodes 'su_pending' '校验控制应答\n保存到pending等待USART3空闲' 12.0 15.25 4.6 0.82 process | Out-Null
    Add-Node $Page $nodes 'su_pre' '搜索 FF 55 12 34 81' 9.55 15.15 4.0 0.64 process | Out-Null
    Add-Node $Page $nodes 'su_len' '读取payload长度' 9.55 14.05 3.5 0.62 process | Out-Null
    Add-Node $Page $nodes 'su_len_ok' '长度为128～128+20000？' 9.55 12.90 4.2 0.68 decision | Out-Null
    Add-Node $Page $nodes 'su_body' '持续拼接DeviceInfo + JPEG + checksum' 9.55 11.65 4.8 0.7 process | Out-Null
    Add-Node $Page $nodes 'su_complete' '完整帧收齐？' 9.55 10.50 3.4 0.66 decision | Out-Null
    Add-Node $Page $nodes 'su_handle' 'CapsuleProtocol_HandleFrame()' 9.55 9.25 4.5 0.66 process | Out-Null
    Add-Node $Page $nodes 'su_check' '长度、总校验和、DeviceInfo标识均正确？' 9.55 7.95 4.9 0.74 decision | Out-Null
    Add-Node $Page $nodes 'su_reject' 'rejected_frames++\n重置解析器' 7.05 6.70 3.8 0.76 error | Out-Null
    Add-Node $Page $nodes 'su_jpeg' '有DQT/DHT则直接复制\n否则插入legacy JPEG头' 9.55 6.55 4.7 0.84 process | Out-Null
    Add-Node $Page $nodes 'su_info' '补充STM版本、ID、运行时间、RTC、JPEG长度' 9.55 5.15 5.0 0.82 process | Out-Null

    Add-Edge $Page $nodes su_irq su_commit | Out-Null
    Add-Edge $Page $nodes su_commit su_wrap | Out-Null
    Add-Edge $Page $nodes su_wrap su_linear '否' | Out-Null
    Add-Edge $Page $nodes su_wrap su_two '是' | Out-Null
    Add-Edge $Page $nodes su_linear su_ring | Out-Null
    Add-Edge $Page $nodes su_two su_ring | Out-Null
    Add-Edge $Page $nodes su_ring su_input | Out-Null
    Add-Edge $Page $nodes su_input su_ctrl | Out-Null
    Add-Edge $Page $nodes su_ctrl su_pending '是' | Out-Null
    Add-Edge $Page $nodes su_ctrl su_pre '否' | Out-Null
    Add-Edge $Page $nodes su_pre su_len | Out-Null
    Add-Edge $Page $nodes su_len su_len_ok | Out-Null
    Add-Edge $Page $nodes su_len_ok su_reject '否' | Out-Null
    Add-Edge $Page $nodes su_len_ok su_body '是' | Out-Null
    Add-Edge $Page $nodes su_body su_complete | Out-Null
    Add-Edge $Page $nodes su_complete su_body '否' | Out-Null
    Add-Edge $Page $nodes su_complete su_handle '是' | Out-Null
    Add-Edge $Page $nodes su_handle su_check | Out-Null
    Add-Edge $Page $nodes su_check su_reject '否' | Out-Null
    Add-Edge $Page $nodes su_check su_jpeg '是' | Out-Null
    Add-Edge $Page $nodes su_jpeg su_info | Out-Null

    Add-Node $Page $nodes 'ss_name' '按胶囊SN生成 Y<SN>.YS 文件名' 16.0 24.15 4.7 0.68 process | Out-Null
    Add-Node $Page $nodes 'ss_pc_idle' '上一幅USART3图片DMA空闲？' 16.0 22.95 4.3 0.68 decision | Out-Null
    Add-Node $Page $nodes 'ss_forward' 'ForwardToPc()\n启动7字节ZAYS 0x81头DMA' 16.0 21.70 4.6 0.8 process | Out-Null
    Add-Node $Page $nodes 'ss_dma1' 'TX完成回调：发送JPEG DMA' 16.0 20.45 4.4 0.66 event | Out-Null
    Add-Node $Page $nodes 'ss_dma2' 'TX完成回调：发送128字节DeviceInfo DMA' 16.0 19.25 4.9 0.68 event | Out-Null
    Add-Node $Page $nodes 'ss_dma3' 'TX完成回调：发送1字节checksum DMA' 16.0 18.05 4.7 0.68 event | Out-Null
    Add-Node $Page $nodes 'ss_dma4' 'TX完成：pc_tx_state = IDLE' 16.0 16.85 4.3 0.66 state | Out-Null
    Add-Node $Page $nodes 'ss_sd' 'AppendCapsuleRecord()\n向.YS追加固定20064字节记录' 16.0 15.45 4.8 0.84 process | Out-Null
    Add-Node $Page $nodes 'ss_sync' '达到同步策略？' 16.0 14.15 3.5 0.66 decision | Out-Null
    Add-Node $Page $nodes 'ss_fsync' 'f_sync()' 16.0 12.95 3.2 0.62 process | Out-Null
    Add-Node $Page $nodes 'ss_done' 'completed_frames++' 16.0 11.75 3.6 0.62 state | Out-Null

    Add-Edge $Page $nodes su_info ss_name | Out-Null
    Add-Edge $Page $nodes ss_name ss_pc_idle | Out-Null
    Add-Edge $Page $nodes ss_pc_idle ss_forward '是' | Out-Null
    Add-Edge $Page $nodes ss_forward ss_dma1 | Out-Null
    Add-Edge $Page $nodes ss_dma1 ss_dma2 | Out-Null
    Add-Edge $Page $nodes ss_dma2 ss_dma3 | Out-Null
    Add-Edge $Page $nodes ss_dma3 ss_dma4 | Out-Null
    Add-Edge $Page $nodes ss_pc_idle ss_sd '否：图片帧丢弃' | Out-Null
    Add-Edge $Page $nodes ss_forward ss_sd '并行继续' | Out-Null
    Add-Edge $Page $nodes ss_sd ss_sync | Out-Null
    Add-Edge $Page $nodes ss_sync ss_fsync '是' | Out-Null
    Add-Edge $Page $nodes ss_sync ss_done '否' | Out-Null
    Add-Edge $Page $nodes ss_fsync ss_done | Out-Null

    Add-Node $Page $nodes 'spc_in' 'USART3 DMA接收PC输入' 16.0 9.85 4.2 0.66 event | Out-Null
    Add-Node $Page $nodes 'spc_zays' '完整ZAYS命令？' 16.0 8.70 3.5 0.66 decision | Out-Null
    Add-Node $Page $nodes 'spc_rtc' '0x02：本地设置SD2058 RTC' 13.8 7.45 4.1 0.68 process | Out-Null
    Add-Node $Page $nodes 'spc_fwd' '其他命令或普通字节\n同步转发到USART2 → nRF RX' 17.2 7.25 4.3 0.82 process | Out-Null
    Add-Node $Page $nodes 'spc_pending' 'Protocol_Process()：\nPC图片DMA空闲后发送pending控制应答' 16.0 5.55 5.0 0.88 process | Out-Null

    Add-Edge $Page $nodes spc_in spc_zays | Out-Null
    Add-Edge $Page $nodes spc_zays spc_rtc '0x02' | Out-Null
    Add-Edge $Page $nodes spc_zays spc_fwd '其他' | Out-Null
    Add-Edge $Page $nodes su_pending spc_pending | Out-Null

    Add-Node $Page $nodes 'stm_irq_note' "TIM1中断：application_milliseconds++\nUART/DMA中断只提交新字节到ring\n图片解析、SD写入都在主循环中执行" 3.15 4.65 5.2 1.25 note | Out-Null
}

function Build-OverviewPage {
    param($Page)
    $nodes = @{}
    Configure-Page $Page 'TX-RX-STM总体链路' 19 13
    Add-Header $Page '图片主链路' 9.5 11.35 16.8 | Out-Null

    Add-Node $Page $nodes 'ov_tx_cap' 'TX\n摄像头采集JPEG\n加速度同步采样' 2.2 9.65 3.4 1.05 state | Out-Null
    Add-Node $Page $nodes 'ov_tx_rf' 'TX Radio\nBEGIN×2 → DATA分片 → END' 5.7 9.65 3.7 1.05 process | Out-Null
    Add-Node $Page $nodes 'ov_rx_rf' 'RX Radio\n12路天线接收与RSSI扫描' 9.5 9.65 3.8 1.05 process | Out-Null
    Add-Node $Page $nodes 'ov_rx_img' 'RX图片模块\n重组、完整性和校验和检查' 13.3 9.65 3.8 1.05 process | Out-Null
    Add-Node $Page $nodes 'ov_stm' 'STM USART2 DMA\n解析FF55123481图片帧' 17.0 9.65 3.5 1.05 state | Out-Null

    Add-Edge $Page $nodes ov_tx_cap ov_tx_rf | Out-Null
    Add-Edge $Page $nodes ov_tx_rf ov_rx_rf '2.4GHz 2Mbps' | Out-Null
    Add-Edge $Page $nodes ov_rx_rf ov_rx_img | Out-Null
    Add-Edge $Page $nodes ov_rx_img ov_stm 'UART 1Mbps' | Out-Null

    Add-Node $Page $nodes 'ov_ack' 'RX→TX反馈\n0x10：图片接收成功\n0x12：请求快速天线扫描' 9.5 7.65 5.0 1.05 event | Out-Null
    Add-Edge $Page $nodes ov_rx_img ov_ack | Out-Null
    Add-Edge $Page $nodes ov_ack ov_tx_rf 'Radio反馈' | Out-Null

    Add-Node $Page $nodes 'ov_sd' 'SD卡\nY<SN>.YS固定长度记录' 14.0 5.55 3.8 1.0 process | Out-Null
    Add-Node $Page $nodes 'ov_pc' 'PC / U盘\nZAYS 0x81：JPEG + DeviceInfo' 18.0 5.55 3.4 1.0 process | Out-Null
    Add-Edge $Page $nodes ov_stm ov_sd 'FatFs' | Out-Null
    Add-Edge $Page $nodes ov_stm ov_pc 'USART3 DMA' | Out-Null

    Add-Header $Page '控制与绑定链路' 6.0 5.65 9.5 | Out-Null
    Add-Node $Page $nodes 'ov_pc_cmd' 'PC控制命令\nZAYS控制帧' 2.0 3.95 3.2 0.82 event | Out-Null
    Add-Node $Page $nodes 'ov_stm_cmd' 'STM\n0x02本地设置RTC\n其他命令透传' 5.5 3.95 3.6 1.0 process | Out-Null
    Add-Node $Page $nodes 'ov_rx_cmd' 'RX\n查询/绑定/解绑本地处理\n出厂配置命令转发TX' 9.4 3.95 4.0 1.0 process | Out-Null
    Add-Node $Page $nodes 'ov_tx_cmd' 'TX\n上电窗口处理0x40～0x46\n控制应答无线重复3次' 13.6 3.95 4.0 1.0 process | Out-Null
    Add-Edge $Page $nodes ov_pc_cmd ov_stm_cmd | Out-Null
    Add-Edge $Page $nodes ov_stm_cmd ov_rx_cmd 'USART2' | Out-Null
    Add-Edge $Page $nodes ov_rx_cmd ov_tx_cmd 'Radio' | Out-Null
    Add-Edge $Page $nodes ov_tx_cmd ov_rx_cmd '应答' | Out-Null
    Add-Edge $Page $nodes ov_rx_cmd ov_stm_cmd '应答' | Out-Null
    Add-Edge $Page $nodes ov_stm_cmd ov_pc_cmd '应答' | Out-Null
}

$resolvedOutput = [System.IO.Path]::GetFullPath($OutputPath)
$outputDirectory = Split-Path -Parent $resolvedOutput
[System.IO.Directory]::CreateDirectory($outputDirectory) | Out-Null

$script:VisioApp = [System.Activator]::CreateInstance(
    [type]::GetTypeFromProgID('Visio.Application'))
if ($null -eq $script:VisioApp) {
    throw 'Unable to create the Visio COM application.'
}
Invoke-ComAction { $script:VisioApp.AlertResponse = 7 }
$document = $null
try {
    Invoke-ComAction { [void]$script:VisioApp.Documents.Add('') }
    Invoke-ComRetry { $script:VisioApp.Documents.Item($script:VisioApp.Documents.Count) } ([ref]$document)
    Invoke-ComAction { $script:VisioApp.Visible = $true }
    $txPage = $null
    Invoke-ComRetry { $document.Pages.Item(1) } ([ref]$txPage)
    Build-TxPage $txPage
    Invoke-ComAction { [void]$document.Pages.Add() }
    $rxPage = $null
    Invoke-ComRetry { $document.Pages.Item($document.Pages.Count) } ([ref]$rxPage)
    Build-RxPage $rxPage
    Invoke-ComAction { [void]$document.Pages.Add() }
    $stmPage = $null
    Invoke-ComRetry { $document.Pages.Item($document.Pages.Count) } ([ref]$stmPage)
    Build-StmPage $stmPage
    Invoke-ComAction { [void]$document.Pages.Add() }
    $overviewPage = $null
    Invoke-ComRetry { $document.Pages.Item($document.Pages.Count) } ([ref]$overviewPage)
    Build-OverviewPage $overviewPage

    if (Test-Path -LiteralPath $resolvedOutput) {
        Remove-Item -LiteralPath $resolvedOutput -Force
    }
    Invoke-ComAction { $document.SaveAs($resolvedOutput) }
    Invoke-ComAction { $document.Close() }
    $document = $null
}
finally {
    if ($null -ne $document) {
        try { $document.Close() } catch { }
    }
    if ($null -ne $script:VisioApp) {
        try { $script:VisioApp.Quit() } catch { }
        try { [void][System.Runtime.InteropServices.Marshal]::FinalReleaseComObject($script:VisioApp) } catch { }
    }
}

Write-Output $resolvedOutput

