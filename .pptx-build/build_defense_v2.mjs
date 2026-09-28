import fs from 'node:fs/promises';
import path from 'node:path';
import { pathToFileURL } from 'node:url';
import { Presentation, PresentationFile } from '@oai/artifact-tool';

const SKILL_DIR='C:\\Users\\dell\\.codex\\plugins\\cache\\openai-primary-runtime\\presentations\\26.905.11957\\skills\\presentations';
const ROOT='E:\\Project\\capsule_project';
const BUILD=path.join(ROOT,'.pptx-build');
const OUT=path.join(ROOT,'output','转正答辩_RX扫描与新旧工程对比_8ms版.pptx');
const PYTHON='C:\\Users\\dell\\.cache\\codex-runtimes\\codex-primary-runtime\\dependencies\\python\\python.exe';
const {resolvePresentationFont,finalizePresentation}=await import(pathToFileURL(path.join(SKILL_DIR,'container_tools','artifact_tool_utils.mjs')).href);
const font=resolvePresentationFont();
const ppt=Presentation.create({slideSize:{width:1280,height:720}});
const K={ink:'#132F40',teal:'#087F8C',blue:'#2E6B88',body:'#415560',soft:'#EAF2F4',line:'#B8C8CE',mist:'#F6F8F9',white:'#FFFFFF',orange:'#B36A27',slate:'#637982'};
function text(s,t,x,y,w,h,size=22,color=K.body,bold=false){let q=s.shapes.add({geometry:'textbox',position:{left:x,top:y,width:w,height:h},fill:'none',line:{fill:'none',width:0}});q.text=t;q.text.style={typeface:font,fontSize:size,color,bold,autoFit:'none'};return q;}
function slide(title,chapter,sub='') {const s=ppt.slides.add();s.background.fill=K.white;text(s,chapter.toUpperCase(),70,27,800,25,15,K.teal,true);text(s,title,70,68,1140,58,39,K.ink,true);if(sub)text(s,sub,72,132,1130,47,19,K.slate);text(s,'转正答辩  /  磁控胶囊内窥镜软件',70,681,760,19,12,K.slate);text(s,String(ppt.slides.items.length).padStart(2,'0'),1172,678,42,22,13,K.slate);return s;}
function note(s,t){s.speakerNotes.textFrame.setText(t);}
function node(s,label,detail,x,y,w,h,fill=K.soft,fs=21){const r=s.shapes.add({geometry:'roundRect',position:{left:x,top:y,width:w,height:h},fill,line:{style:'solid',fill:K.line,width:1},borderRadius:12});text(s,label,x+12,y+13,w-24,36,fs,K.ink,true);text(s,detail,x+12,y+53,w-24,h-58,16,K.body);return r;}
function arrow(s,a,b,label,x,y,w=110){s.shapes.connect(a,b,{kind:'straight',fromSide:'right',toSide:'left',line:{style:'solid',fill:K.teal,width:2},head:{type:'arrow',width:'med',length:'med'}});text(s,label,x,y,w,25,14,K.teal,true);}
function rule(s,y,x=72,w=1135){s.shapes.add({geometry:'line',position:{left:x,top:y,width:w,height:0},line:{style:'solid',fill:K.line,width:1}});}

// 1 Cover: minimal and personal details remain editable.
{
 const s=ppt.slides.add();s.background.fill=K.ink;
 text(s,'试用期转正答辩',82,76,800,40,23,'#79CDD2',true);
 text(s,'工作回顾与\n工程重构实践',78,177,1000,175,56,K.white,true);
 text(s,'软件开发岗位  /  [姓名]  /  [部门]  /  [日期]',84,554,1000,40,21,'#D4E3E8');
 text(s,'磁控胶囊项目是工作案例之一，重点说明技术理解、改造思路与交付方式',84,615,1090,32,17,'#A9C1CA');
 note(s,'补充姓名、岗位、部门、日期。开场建议先说试用期的主要职责，再说明胶囊项目是其中一个能体现系统理解和工程实践的案例。');
}
// 2 Scorecard map
{
 const s=slide('试用期工作内容与考核表对应','01  工作总览','答辩主线围绕“理解工作、完成任务、形成交付、持续改进”。');
 const rows=[['20%','产品与系统理解','产品构成、主控流程、通信链路和异常路径'],['30%','代码开发与缺陷修复','按规范开发、定位问题、控制修改影响'],['20%','交接与版本迭代','接收资料，承担功能维护与迭代，配合验证'],['20%','法规标准与软件文档','理解适用标准，按流程输出可追溯资料'],['10%','专项任务','按期处理临时任务，及时反馈风险和进度']];
 rows.forEach((r,i)=>{let y=206+i*72;text(s,r[0],83,y,90,35,25,K.teal,true);text(s,r[1],185,y,280,35,22,K.ink,true);text(s,r[2],487,y,690,40,19,K.body);rule(s,y+50,82,1095)});
 text(s,'职业素养（20%）',83,586,205,35,19,K.teal,true);text(s,'工作态度　责任心　执行力　沟通协调　团队协作',296,586,850,36,19,K.ink,true);
 note(s,'内容对应答辩表五项工作内容与五项职业素养。为避免替本人编造绩效，每一项后续页都留有具体经历、数据或文件编号的填写位置。');
}
// 4 Overall device data chain
{
 const s=slide('产品系统与图像数据链路','02  项目理解','胶囊端采图与编码，接收端选天线和转发，STM32 校验后送 PC 与 SD。');
 const y=258,h=138,w=160,xs=[66,267,468,669,870,1071];
 const ns=[node(s,'OV7676','图像传感器\n480×480 YUV422',xs[0],y,w,h,K.soft,20),node(s,'CX93510','采集同步\nJPEG 压缩 / 帧 RAM',xs[1],y,w,h,K.soft,20),node(s,'nRF TX','SPI 读帧 RAM\n分片无线发送',xs[2],y,w,h,K.soft,20),node(s,'nRF RX','12 路天线\n选路 / 收包',xs[3],y,w,h,K.soft,20),node(s,'STM32','USART2 解析\n校验 / 转发 / SD',xs[4],y,w,h,K.soft,20),node(s,'PC','图像显示\n设备信息 / 文件',xs[5],y,w,h,K.soft,20)];
 const labs=['DVP 并行','主机 SPI','2.4 GHz Radio','USART2\n1 Mbit/s','USART3\n921600 bit/s'];
 for(let i=0;i<5;i++)arrow(s,ns[i],ns[i+1],labs[i],xs[i]+w+3,y+50,67);
 text(s,'数据归属',80,462,120,30,17,K.teal,true);text(s,'像素流',198,462,138,30,18,K.ink,true);text(s,'JPEG 帧数据',400,462,180,30,18,K.ink,true);text(s,'固定长度无线包',619,462,205,30,18,K.ink,true);text(s,'校验后的图片帧',861,462,215,30,18,K.ink,true);
 text(s,'SD 卡保存固定长度 .YS 记录，PC 可接收实时图像并查看历史记录。',80,549,1090,40,21,K.teal,true);
 note(s,'源码依据：nrf_tx/examples/peripheral/radio/TX/ov7676.c、cx93510.c、image.c；RX 的 antenna_manager/radio_link；stm32F103RET6/stm_project/Core/Src/capsule_protocol.c、sd_storage.c。箭头和流程框均为可编辑 PowerPoint 对象。');
}
// 5 Detailed capture path
{
 const s=slide('图像怎样变成可发送的 JPEG','03  采集与编码','关键边界：OV7676 输出原始像素；CX93510 负责采集、压缩和缓存；nRF 负责读取与传输。');
 const cam=node(s,'OV7676','曝光成像\n并行 DVP 输出\nYUV422：Y0/U/Y1/V',85,263,255,183,'#E8F2F4',23);
 const cx=node(s,'CX93510','DVP 同步采集\nJPEG 编码器处理\n结果写入帧 RAM',500,263,290,183,'#DDECEF',23);
 const rf=node(s,'nRF52832','通过 SPI 访问 CX93510\n读取帧块状态、地址和长度\n每个 DATA 分片按需取数',918,263,285,183,'#E8F2F4',22);
 arrow(s,cam,cx,'DVP 像素流',347,321,130);arrow(s,cx,rf,'SPI 读帧 RAM',798,321,128);
 text(s,'控制路径',88,212,115,27,16,K.orange,true);text(s,'nRF 通过 CX93510 的 I²C 代理配置 OV7676，读回关键寄存器确认输出格式。',218,208,960,35,18,K.body);
 text(s,'采集顺序',85,503,125,30,17,K.teal,true);text(s,'唤醒摄像头并预热',236,503,220,33,19,K.ink,true);text(s,'启动单帧采集与 JPEG 压缩',490,503,330,33,19,K.ink,true);text(s,'JPEG 留在帧 RAM，nRF 分片读取',844,503,355,33,19,K.ink,true);
 text(s,'当前 TX 不把整张 JPEG 复制到 nRF 的 image_buff[]；发送 DATA 时从 CX93510 帧 RAM 读取对应区间。',86,581,1105,50,19,K.teal,true);
 note(s,'设备型号以当前源码为准：OV7676（不是 OV7766）。OV7676 经 CX93510 内部 I²C 主机配置，像素经并行 DVP 送到 CX93510；CX93510 在有限单帧模式下采集，JPEG 数据与帧块信息进入其帧 RAM；nRF52832 通过 SPI 读取帧信息并逐片读 JPEG。当前正式无线协议只发送原始图像块，不将旧版 IMG/CRC16 分块写法当作当前协议。来源：TX/ov7676.c、cx93510.c、image.c；doc/01-通信协议.md；doc/03-TX软件框架.md。');
}
// 6 Link to host and storage
{
 const s=slide('无线传输、接收与 PC 转发','04  通信链路','各段使用不同帧格式，排查问题时先确认数据停在哪条链路。');
 const xs=[82,365,648,931], y=258,w=230,h=132;
 const n1=node(s,'nRF TX','BEGIN / DATA / END\n每包 254 B\nDATA 最多 242 B 图像',xs[0],y,w,h,K.soft,20);
 const n2=node(s,'nRF RX','CRC 与 SN 匹配\n12 路天线选路\n无线包经 UART 转发',xs[1],y,w,h,K.soft,20);
 const n3=node(s,'STM32','USART2 流式解析\n长度 / Checksum 校验\n补 DeviceInfo / JPEG',xs[2],y,w,h,K.soft,20);
 const n4=node(s,'PC + SD','USART3 DMA 发 PC\nSD 写入 20064 B 记录\n每 10 次成功写入同步',xs[3],y,w,h,K.soft,20);
 arrow(s,n1,n2,'Radio',312,304,60);arrow(s,n2,n3,'USART2',595,304,60);arrow(s,n3,n4,'USART3 / SDIO',878,304,100);
 text(s,'RX → STM 帧',86,466,160,30,17,K.teal,true);text(s,'FF 55 12 34 81 + 长度 + 128 B DeviceInfo + JPEG + 校验',254,464,922,34,19,K.ink);
 text(s,'STM → PC 帧',86,516,160,30,17,K.teal,true);text(s,'ZAYS 81 + 长度 + JPEG + DeviceInfo + 校验；图片按四段 DMA 顺序发送',254,514,950,42,18,K.ink);
 text(s,'无线协议 / UART 帧头 / SD 记录格式不能混为同一种“胶囊协议”。',86,586,1090,35,20,K.orange,true);
 note(s,'TX-RX 无线协议依据 doc/01-通信协议.md；RX到STM和STM到PC依据 doc/08-STM软件框架.md 与当前 capsule_protocol.c。协议字段和校验要以这些当前文件为准。');
}
// 6 RX antenna scanning: old and current flowcharts
{
 const s=slide('RX 天线扫描：旧版与新版','05  天线选路','旧版独立扫描并锁定一路；新版与 TX 协商快速 SN 广播，保留三路候选。');
 text(s,'TYGD31 旧版',84,205,470,32,22,K.slate,true);
 text(s,'当前工程',665,205,500,32,22,K.teal,true);
 const flowStep=(x,y,w,title,detail,fill)=>{s.shapes.add({geometry:'roundRect',position:{left:x,top:y,width:w,height:76},fill,line:{style:'solid',fill:K.line,width:1}});text(s,title,x+16,y+9,w-32,31,20,K.ink,true);text(s,detail,x+16,y+43,w-32,30,16,K.body)};
 const old=[
  ['逐路扫描','12 路天线每 3 ms 切换一次'],
  ['收集 RSSI','全局累计 36 个正值，各路样本数不固定'],
  ['只选一路','每路保留最强一次，选幅值最小者'],
  ['锁定并监测','1200 ms 无完整图片则重新扫描']
 ];
 const now=[
  ['寻找目标 END','绑定后每路 8 ms；RX 发 0x12 请求'],
  ['TX 配合快速广播','暂停图片，回 0x11；每 8 ms 播 SN，最长 1800 ms'],
  ['正式扫描 12 路','每路 32 ms × 4 轮；只计 CRC / SN 均有效的 0x05'],
  ['排序并锁定','均值、≥2 样本优先；前三候选依次切换']
 ];
 old.forEach((a,i)=>{let y=247+i*91;flowStep(82,y,505,a[0],a[1],K.mist);if(i<3)text(s,'▼',327,y+76,30,16,16,K.slate,true)});
 now.forEach((a,i)=>{let y=247+i*91;flowStep(660,y,528,a[0],a[1],i===3?'#DDECEF':K.soft);if(i<3)text(s,'▼',910,y+76,30,16,16,K.teal,true)});
 text(s,'旧版失联：直接回到扫描',82,620,480,28,18,K.slate,true);
 text(s,'新版失联：先换候选；全部失败再寻 END',660,620,540,28,18,K.teal,true);
 note(s,'旧版源码：TYGD31-2.4GHZ/TYGD_RX/Application/antenna.c。ANTENNA_STATUS_SCAN 每3ms轮换12路，扫描函数读 radio_rssi_value>0，36为全局累计样本数；每路记录最小RSSI幅值，选择单一路；ANTENNA_STATUS_DETECT每1200ms无完整图片时回到扫描。新版源码：nrf_rx/examples/peripheral/radio/receiver/antenna_manager.c 与 config.h、nrf_tx/examples/peripheral/radio/TX/image.c 与 config.h。绑定后SEEK_END每路8ms寻找目标END，在TX应答窗口发0x12；TX收到后发0x11并最多1800ms每8ms广播SN；RX正式FAST_SCAN每路32ms、4轮，总1536ms，只统计CRC正确、SN匹配的0x05包；以至少2个样本优先，再比较平均RSSI，保留前三候选。LOCKED中若目标包超时1500ms、完整图超时4000ms或连续3帧失败，先切下一候选，耗尽再SEEK_END。实际无线与移动性能仍需实测。');
}
// 7 Overall old/current architecture comparison
{
 const s=slide('TX / RX 新旧工程整体对比','06  工程重构','围绕取图、唤醒、等待、空闲和选路，比较代码中的实际实现。');
 text(s,'对比点',83,205,165,32,17,K.teal,true);text(s,'TYGD31 旧工程',292,205,365,32,17,K.slate,true);text(s,'当前工程',688,205,492,32,17,K.teal,true);
 const rows=[
  ['图片数据','整张 JPEG 先复制到 nRF RAM','发送 DATA 时按片读 CX93510 帧 RAM'],
  ['周期唤醒','TIMER2 固定周期唤醒；主循环 WFI','RTC2 产生 500 ms 请求；空闲时 WFE'],
  ['业务等待','拍摄预热和取图轮询使用 delay_ms','TIMER1 的 1 ms 时基推进预热、分片与 ACK'],
  ['SPI 与帧 RAM','SPI0 常开；整图搬走后关闭 PDRAM','帧 RAM 保持供分片读取；事务结束关 SPIM0'],
  ['天线选路','单次最强 RSSI，锁定一路','目标 SN + CRC 过滤，均值排序，前三候选']
 ];
 rows.forEach((r,i)=>{let y=246+i*70;text(s,r[0],83,y,175,49,18,K.ink,true);text(s,r[1],292,y,360,55,17,K.body);text(s,'→',653,y+2,32,31,20,K.teal,true);text(s,r[2],688,y,500,55,17,K.ink,true);rule(s,y+56,83,1100)});
 text(s,'说明：1 ms 时基用于业务调度；芯片初始化及 CX93510 内部轮询仍有短阻塞等待。',83,617,1110,40,16,K.orange,true);
 note(s,'依据：TYGD31-2.4GHZ/TYGD_TX/Application/domain.c、image.c、Application/antenna.c，以及当前 nrf_tx/examples/peripheral/radio/TX/main.c、image.c、cx93510.c，nrf_rx/examples/peripheral/radio/receiver/antenna_manager.c，doc/03-TX软件框架.md、doc/07-新旧工程对比.md。旧TX domain在初始化timer2后使用WFI，image_read有delay_ms(1)轮询，旧CX采集前等待25ms；SPI0正常空闲没有禁用，整幅JPEG读入image_buff后可拉低旧板PDRAM。新版RTC2负责低功耗500ms唤醒，TIMER1 1ms活动时基用于预热、分片和ACK的时间戳，主循环WFE；需要逐片读JPEG时保持CX帧RAM供电，图片事务结束关闭nRF SPIM0。不能概括为新版完全没有delay：CX93510初始化和单帧采集轮询仍使用短阻塞nrf_delay_ms。选路差异见上一页。该页只陈述实现差异，不宣称尚未测得的功耗或链路性能改进。');
}
// 9 standards migration and change-registration impact
{
 const s=slide('新旧标准差异与变更注册','07  法规合规','关键变化：安全与性能要求升级，注册资料需按新标准重新评估。');
 text(s,'标准更新',82,208,160,25,15,K.teal,true);text(s,'旧版',330,208,300,25,15,K.slate,true);text(s,'新版',650,208,300,25,15,K.slate,true);text(s,'变化重点',960,208,220,25,15,K.slate,true);
 const rows=[
  ['通用安全','GB 9706.1-2007','GB 9706.1-2020','从基本安全扩展到基本安全＋基本性能'],
  ['电磁兼容','YY 0505-2012','YY 9706.102-2021','按新版 EMC 要求重新评估与验证'],
  ['风险管理','YY/T 0316-2016','GB/T 42062-2022\nYY/T 1437-2023 指南','更新风险管理依据及过程/记录']
 ];
 rows.forEach((r,i)=>{let y=252+i*74;text(s,r[0],82,y,205,43,19,K.ink,true);text(s,r[1],330,y,290,43,18,K.body);text(s,r[2],650,y,290,50,18,K.ink,true);text(s,r[3],960,y,230,51,16,K.body);rule(s,y+58,82,1105)});
 text(s,'变更注册主线',82,495,180,27,16,K.teal,true);
 text(s,'差距分析  →  风险 / 设计更新  →  检验与资料更新  →  变更注册',282,493,900,32,19,K.ink,true);
 text(s,'另核对专用标准：GB 9706.218-2021（内窥镜设备）',82,547,1090,30,17,K.slate);
 text(s,'注：具体适用条款、资料范围与期限由法规/注册负责人结合注册单元确认。',82,595,1100,30,15,K.orange,true);
 note(s,'用于答辩概述新旧标准迁移，不替代法规评估。差异表只列标准层级的变化方向，不声称逐条条款差异。GB 9706.1-2007已废止，由GB 9706.1-2020替代；YY 0505-2012由YY 9706.102-2021替代。风险管理按用户给定背景：YY/T 0316-2016为原项目采用依据，切换至GB/T 42062-2022及YY/T 1437-2023应用指南；YY/T 1437-2023直接替代的是YY/T 1437-2016。变更注册适用性、注册单元、资料清单与过渡期限须由法规/注册负责人确认；过渡期通用政策详见国家药监局2023年第14号及官方解读。产品专用标准适用性也须确认；“9706.128”未检索到，胶囊内窥镜相关专用标准检索为GB 9706.218-2021。\n\n官方来源：GB 9706.1-2020 https://openstd.samr.gov.cn/bzgk/std/newGbInfo?hcno=74E8A9884B75808BF9369E2E25196F53\nGB 9706.1-2007 https://openstd.samr.gov.cn/bzgk/std/newGbInfo?hcno=6B3CB82E3FD260F2AF530E71D4836D49\nYY 9706.102-2021 https://std.samr.gov.cn/hb/search/stdHBDetailed?id=BFEAFC933D025285E05397BE0A0A7BFB\nGB 9706.218-2021 https://openstd.samr.gov.cn/bzgk/std/newGbInfo?hcno=963619BBF8282C5927A353D689EF4340\nGB/T 42062-2022 https://openstd.samr.gov.cn/bzgk/std/newGbInfo?hcno=1148CB082527B776560903948B22DAB0\nYY/T 1437-2023 https://std.samr.gov.cn/hb/search/stdHBDetailed?id=00D333A21F1A492AE06397BE0A0A2B32\n变更注册过渡政策官方解读 https://yjj.sh.gov.cn/qtgzwj/20230316/8b076216db34470bbcc7e20d6f6ea517.html');
}
// 10 Validation and regulatory learning applied to work
{
 const s=slide('流程文件、验证与可追溯交付','08  质量意识','软件实现只是交付的一部分，需求、风险、测试和变更记录也要连起来。');
 const stages=[['需求','来源 / 编号','[待补]'],['设计','接口 / 状态','[待补]'],['实现','提交 / 评审','[待补]'],['验证','用例 / 结果','[待补]'],['发布维护','版本 / 变更','[待补]']];
 const stageShapes=stages.map((a,i)=>{let x=75+i*228;return node(s,a[0],a[1]+'\n'+a[2],x,267,188,111,i===2?'#DDECEF':K.mist,20)});
 for(let i=0;i<4;i++)s.shapes.connect(stageShapes[i],stageShapes[i+1],{kind:'straight',fromSide:'right',toSide:'left',line:{style:'solid',fill:K.teal,width:2},head:{type:'arrow',width:'med',length:'med'}});
 text(s,'已梳理资料',82,462,180,32,17,K.teal,true);text(s,'通信协议、目录结构、TX/RX/STM 软件框架、新旧工程对比。',266,462,900,35,19,K.ink);
 text(s,'待补正式证据',82,520,180,32,17,K.orange,true);text(s,'公司流程文件编号、适用标准清单、需求追踪、验证报告、评审和版本记录。',266,520,930,43,18,K.body);
 text(s,'当前工程已知边界：STM 图片解析尚无帧静默超时，列入后续风险评估与验证。',82,595,1090,34,18,K.orange,true);
 note(s,'项目仓库已有技术文档，但不能据此推断公司受控的法规标准清单、完整生命周期文档或正式验证已完成。把真实文件名、编号、版本和审核状态填入。软件生命周期要求可参照YY/T 0664-2020理解，系统级确认不应被代码测试替代。');
}
// 11 Professionalism and special assignments
{
 const s=slide('工作方式与职业素养','09  协作与执行','把五项职业素养落到具体事件，避免只做自我评价。');
 const items=[['工作态度','[遇到复杂任务时的投入与推进例子]'],['责任心','[主动发现风险并跟踪到闭环的例子]'],['执行力','[按计划完成并反馈进度的例子]'],['沟通协调','[与测试 / 硬件 / 上位机协作案例]'],['团队协作','[分工、互相支持与共同交付案例]']];
 items.forEach((r,i)=>{let y=220+i*67;text(s,String(i+1).padStart(2,'0'),85,y,60,31,17,K.teal,true);text(s,r[0],162,y,210,34,21,K.ink,true);text(s,r[1],392,y,770,39,19,K.body);rule(s,y+45,85,1085)});
 text(s,'专项任务：[任务背景]  /  [本人负责]  /  [交付时间]  /  [结果或复盘]',85,589,1090,40,18,K.orange,true);
 note(s,'对应职业素养的五项评分标准和10%的专项任务权重。请使用真实事件，写明时间、协作对象、个人责任、反馈方式和可验证结果。');
}
// 11 Work review moved to the final content position
{
 const s=slide('试用期工作主线','10  工作回顾','项目任务以外，重点呈现个人负责范围、交付证据和协作过程。');
 text(s,'工作内容',80,218,300,38,18,K.teal,true);text(s,'本人承担与交付',428,218,340,38,18,K.teal,true);text(s,'证明材料',850,218,280,38,18,K.teal,true);
 const rr=[['熟悉产品与代码','[负责模块 / 关键流程 / 上手成果]','[交接资料 / 学习记录]'],['开发与问题闭环','[功能迭代 / 缺陷定位 / 修复结果]','[提交 / 缺陷单 / 评审]'],['验证与跨团队配合','[联调、验证支持、问题同步]','[测试记录 / 协作案例]'],['专项任务','[任务名称、时限、结果]','[任务记录 / 汇报记录]']];
 rr.forEach((r,i)=>{let y=277+i*71;text(s,r[0],80,y,300,40,21,K.ink,true);text(s,r[1],428,y,400,46,19,K.body);text(s,r[2],850,y,340,46,18,K.slate);rule(s,y+51,80,1100)});
 text(s,'答辩建议：每件事按“背景 → 本人动作 → 验证证据 → 结果/复盘”讲清楚。',82,594,1090,41,20,K.teal,true);
 note(s,'根据本人真实试用期工作填写此页。最好选择两到三个最能说明能力的案例，避免只说“参与了”“完成了”，需要讲清本人负责内容和证据。此页已从第3页移至总结前，作为答辩内容的最后一页。');
}
// 12 close
{
 const s=ppt.slides.add();s.background.fill=K.ink;
 text(s,'总结与下一阶段',78,71,960,43,24,'#79CDD2',true);
 text(s,'从系统理解到\n可追溯交付',78,166,990,147,53,K.white,true);
 text(s,'下一阶段重点',84,387,230,34,19,'#79CDD2',true);
 text(s,'补齐链路与移动场景验证；完善异常恢复和回归测试；按流程沉淀需求、风险和验证记录。',84,438,1070,76,23,K.white);
 text(s,'申请转正  /  [本人补充一句具体承诺]',84,576,1040,42,22,'#D3E4E8',true);
 note(s,'结尾请补充你愿意承担的下一阶段具体职责和可检查目标。避免承诺尚未评估过的量化指标。');
}

await fs.mkdir(BUILD,{recursive:true});await fs.mkdir(path.dirname(OUT),{recursive:true});
const candidate=path.join(BUILD,'candidate-v5.pptx');await (await PresentationFile.exportPptx(ppt)).save(candidate);
for(let i=0;i<ppt.slides.items.length;i++){const blob=await ppt.export({slide:ppt.slides.items[i],format:'png',scale:1});await fs.writeFile(path.join(BUILD,`v5-slide-${String(i+1).padStart(2,'0')}.png`),new Uint8Array(await blob.arrayBuffer()));}
const result=await finalizePresentation({workspaceDir:ROOT,candidatePath:candidate,finalPath:OUT,pythonExecutable:PYTHON,integrityValidatorPath:path.join(SKILL_DIR,'container_tools','inspect_presentation_package_integrity.py'),layoutValidatorPath:path.join(SKILL_DIR,'container_tools','inspect_presentation_layout_geometry.py'),layoutArgs:['--expected-slide-size-emu','12192000,6858000','--validate-heading-fit'],explicitTotalSlideCount:12,requiredNativeTableOwnerSlides:[],requiredNativeChartOwnerSlides:[],fontPolicy:{basis:'design',families:[font]},verifyArtifactToolImport:true,receiptPath:path.join(BUILD,'validation-v5.json')});
console.log(JSON.stringify({out:OUT,result},null,2));
