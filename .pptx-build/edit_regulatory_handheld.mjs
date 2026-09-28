import fs from 'node:fs/promises';
import path from 'node:path';
import {pathToFileURL} from 'node:url';
import {createHash} from 'node:crypto';
import {FileBlob, PresentationFile} from '@oai/artifact-tool';

const ROOT='E:\\Project\\capsule_project';
const BUILD=path.join(ROOT,'.pptx-build');
const SRC=path.join(ROOT,'output','转正答辩_修订版_工作与项目重点.pptx');
const OUT=path.join(ROOT,'output','转正答辩_法规变更与手持控制器_完成版.pptx');
const SKILL_DIR='C:\\Users\\dell\\.codex\\plugins\\cache\\openai-primary-runtime\\presentations\\26.905.11957\\skills\\presentations';
const PYTHON='C:\\Users\\dell\\.cache\\codex-runtimes\\codex-primary-runtime\\dependencies\\python\\python.exe';
const {finalizePresentation}=await import(pathToFileURL(path.join(SKILL_DIR,'container_tools','artifact_tool_utils.mjs')).href);

const ppt=await PresentationFile.importPptx(await FileBlob.load(SRC));
const sourceSha256=createHash('sha256').update(await fs.readFile(SRC)).digest('hex');
const K={ink:'#132F40',teal:'#087F8C',body:'#415560',line:'#B8C8CE',soft:'#EAF2F4',slate:'#637982',orange:'#B36A27',white:'#FFFFFF'};
const font='Arial';
function t(s,value,x,y,w,h,size=20,color=K.body,bold=false){
  const q=s.shapes.add({geometry:'textbox',position:{left:x,top:y,width:w,height:h},fill:'none',line:{fill:'none',width:0}});
  q.text=value; q.text.style={typeface:font,fontSize:size,color,bold,autoFit:'none'}; return q;
}
function rule(s,y,x=80,w=1110){s.shapes.add({geometry:'line',position:{left:x,top:y,width:w,height:0},line:{style:'solid',fill:K.line,width:1}});}
function frame(s,chapter,title,subtitle){
  if(s.background)s.background.fill=K.white;
  t(s,chapter,70,27,800,25,15,K.teal,true);
  t(s,title,70,68,1140,58,39,K.ink,true);
  t(s,subtitle,72,132,1130,47,19,K.slate);
  t(s,'转正答辩  /  磁控胶囊内窥镜软件',70,681,760,19,12,K.slate);
}
function notes(s,value){s.speakerNotes.textFrame.setText(value);}

// Keep all user-edited pages and the three embedded figures; add one standalone handheld page.
const handheld=ppt.slides.insert({after:ppt.slides.getItem(8)}).slide;
frame(handheld,'07  手持控制器','手持控制器：一键保存当前照片','按键触发 STM8S103F2P6，经 HC-04S 蓝牙模块向 PC 发送保存指令。');
const xs=[82,337,644,924],ws=[190,260,220,260],yy=267,hh=143;
const labels=[['按键','用户按下“保存”'],['STM8S103F2P6','识别按键并发出指令'],['HC-04S','蓝牙发送'],['PC 上位机','保存当前显示照片']];
for(let i=0;i<labels.length;i++){
  handheld.shapes.add({geometry:'roundRect',position:{left:xs[i],top:yy,width:ws[i],height:hh},fill:i===3?'#E4F2F4':'#F3F9FA',line:{style:'solid',fill:K.line,width:1},borderRadius:10});
  t(handheld,labels[i][0],xs[i]+15,yy+27,ws[i]-30,39,i===1?22:23,K.ink,true);
  t(handheld,labels[i][1],xs[i]+15,yy+84,ws[i]-30,40,16,K.body);
  if(i<3){const gap=xs[i+1]-xs[i]-ws[i];t(handheld,'→',xs[i]+ws[i]+(gap-40)/2,yy+51,40,42,32,K.teal,true);}
}
t(handheld,'应用命令只有 1 条',85,476,310,35,22,K.teal,true);
t(handheld,'保存 PC 当前照片',400,476,435,35,22,K.ink,true);
rule(handheld,531,85,1090);
t(handheld,'命令码 / 报文格式：[待补]    按键消抖、蓝牙断连及 PC 应答验证：[待补]',85,554,1090,43,18,K.slate);
notes(handheld,'手持控制器信息由用户提供：主控 STM8S103F2P6，控制 HC-04S，应用协议仅一条，用于通过蓝牙控制 PC 保存当前照片。具体按键电路、串口参数、命令码及报文格式未提供，留待本人补充。');

// Rebuild the original standards slide in the same typography and color system.
const legal1=ppt.slides.getItem(10);
for(const sh of [...legal1.shapes.items])sh.delete();
frame(legal1,'08  法规合规','适用标准更新与注册依据','按通用安全、EMC、内窥镜专用要求和风险管理逐项做差距分析。');
const col=[83,307,595,855], widths=[210,270,245,344];
['领域','旧依据','当前依据','本次关注'].forEach((x,i)=>t(legal1,x,col[i],207,widths[i],28,17,i===0?K.teal:K.slate,true));
const stdRows=[
 ['通用安全','GB 9706.1-2007','GB 9706.1-2020','基本性能、绝缘与耐压复核'],
 ['电磁兼容','YY 0505-2012','YY 9706.102-2021','发射与抗扰度影响评价'],
 ['内窥镜专用','GB 9706.19-2000','GB 9706.218-2021','确认系统及附件适用条款'],
 ['风险管理','YY/T 0316-2016','GB/T 42062-2022','按 YY/T 1437-2023 更新记录'],
];
stdRows.forEach((r,i)=>{const y=253+i*74; t(legal1,r[0],col[0],y,205,44,19,K.ink,true);t(legal1,r[1],col[1],y,270,44,17,K.body);t(legal1,r[2],col[2],y,250,44,17,K.ink,true);t(legal1,r[3],col[3],y,340,45,17,K.body);rule(legal1,y+58,83,1107);});
t(legal1,'注册判断',83,565,150,30,18,K.teal,true);
t(legal1,'标准更新与实物变更均需差距评价；变更注册范围由注册人结合安全有效性影响确认。',231,565,950,53,19,K.ink,true);
notes(legal1,[
 '标准来源（全国标准信息公共服务平台）：',
 'GB 9706.1-2020 https://openstd.samr.gov.cn/bzgk/std/newGbInfo?hcno=74E8A9884B75808BF9369E2E25196F53',
 'YY 9706.102-2021 https://std.samr.gov.cn/hb/search/stdHBDetailed?id=BFEAFC933D025285E05397BE0A0A7BFB',
 'GB 9706.218-2021 https://std.samr.gov.cn/gb/search/gbDetailed?id=D275887A544B7721E05397BE0A0AFB27',
 'GB/T 42062-2022 https://std.samr.gov.cn/gb/search/gbDetailed?id=EB58F4DA9167B2A2E05397BE0A0A7D33',
 'YY/T 1437-2023 https://std.samr.gov.cn/hb/search/stdHBDetailed?id=00D333A21F1A492AE06397BE0A0A2B32',
 '本页“本次关注”为结合用户变更事项推导的评估方向，非标准逐条引用；GB 9706.218 的产品适用性仍由注册人确认。',
].join('\n'));

// A second legal page makes the actual change assessment concrete without inventing component data or hipot values.
const legal2=ppt.slides.insert({after:legal1}).slide;
frame(legal2,'08  法规合规','电池与器件变更的评估重点','手持控制器整机型号不变；各项变更仍需证明安全、有效和质量可控。');
const c=[84,343,704],cw=[246,348,480];
['变更项','需核查的影响','待补证据'].forEach((x,i)=>t(legal2,x,c[i],206,cw[i],30,18,i===0?K.teal:K.slate,true));
const changeRows=[
 ['图像记录仪电池更换','供电 / 充电、保护、温升、续航','[新旧电池规格对照、验证结果]'],
 ['手持控制器电池 A→B','整机型号不变；核查电气与连接兼容','[A/B 料号、参数、测试记录]'],
 ['图像记录仪器件改动','电气安全、EMC、图像性能受影响项','[器件清单、等效分析、回归记录]'],
];
changeRows.forEach((r,i)=>{const y=252+i*91;t(legal2,r[0],c[0],y,cw[0],65,18,K.ink,true);t(legal2,r[1],c[1],y,cw[1],65,17,K.body);t(legal2,r[2],c[2],y,cw[2],65,17,K.slate);rule(legal2,y+74,84,1095);});
t(legal2,'耐压变化',84,547,150,33,20,K.teal,true);
t(legal2,'按新版绝缘路径、试验点和判定复核；旧值→新值、样机实测：[待补]',240,547,945,58,19,K.ink,true);
t(legal2,'资料闭环：差异表、风险管理更新、针对性验证；注册路径由注册负责人判定。',84,625,1100,39,17,K.slate);
notes(legal2,[
 '用户提供的变更事实：图像记录仪板子电池更换；手持设备控制器电池由 A 换到 B，整机型号不变；图像记录仪器件改动；产品涉及耐压变化。未提供具体料号、工作电压、旧版/新版耐压值与检验结果，故在页内留空。',
 '注册依据：国家药监局《医疗器械注册与备案管理办法》第79、80条，实质性变化可能影响安全有效时办理变更注册，技术审评重点针对变化部分。https://english.nmpa.gov.cn/2024-06/05/c_993242.htm',
 '通用安全标准 GB 9706.1-2020：https://openstd.samr.gov.cn/bzgk/std/newGbInfo?hcno=74E8A9884B75808BF9369E2E25196F53',
 '耐压试验点、数值与判定须结合产品结构及标准适用条款由测试/注册团队确定；本页不预设具体数值。',
].join('\n'));

// Normalize visible page numbers after inserting two pages, without altering other user content.
for(let i=0;i<ppt.slides.items.length;i++){
  const s=ppt.slides.items[i]; let found=false;
  for(const sh of s.shapes.items){
    if(sh.position?.left>=1160 && sh.position?.top>=665 && /^\d{2}$/.test(sh.text?.toString?.()??'')){
      sh.text=String(i+1).padStart(2,'0'); found=true;
    }
  }
  if(!found && s!==ppt.slides.items[0] && s!==ppt.slides.items[ppt.slides.items.length-1])
    t(s,String(i+1).padStart(2,'0'),1172,678,42,22,13,K.slate);
}

await fs.mkdir(BUILD,{recursive:true});
const candidate=path.join(BUILD,'candidate-regulatory-handheld.pptx');
await (await PresentationFile.exportPptx(ppt)).save(candidate);
for(let i=0;i<ppt.slides.items.length;i++){
  const blob=await ppt.export({slide:ppt.slides.items[i],format:'png',scale:1});
  await fs.writeFile(path.join(BUILD,`reg-slide-${String(i+1).padStart(2,'0')}.png`),new Uint8Array(await blob.arrayBuffer()));
}
const result=await finalizePresentation({
  workspaceDir:ROOT,candidatePath:candidate,finalPath:OUT,pythonExecutable:PYTHON,
  integrityValidatorPath:path.join(SKILL_DIR,'container_tools','inspect_presentation_package_integrity.py'),
  layoutValidatorPath:path.join(SKILL_DIR,'container_tools','inspect_presentation_layout_geometry.py'),
  layoutArgs:['--expected-slide-size-emu','12192000,6858000','--validate-heading-fit'],
  explicitTotalSlideCount:15,requiredNativeTableOwnerSlides:[],requiredNativeChartOwnerSlides:[],
  fontPolicy:{basis:'reference',families:['Arial'],referencePath:SRC,referenceSha256:sourceSha256},
  verifyArtifactToolImport:true,receiptPath:path.join(BUILD,'validation-regulatory-handheld-final.json'),
});
console.log(JSON.stringify({out:OUT,slides:ppt.slides.items.length,result},null,2));
