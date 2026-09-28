import fs from 'node:fs/promises';
import path from 'node:path';
import {FileBlob, PresentationFile} from '@oai/artifact-tool';

const root='E:\\Project\\capsule_project';
const src=path.join(root,'output','转正答辩_修订版_工作与项目重点.pptx');
const ppt=await PresentationFile.importPptx(await FileBlob.load(src));
console.log('slides',ppt.slides.items.length);
const snap=await ppt.inspect({kind:'slide,textbox,image',maxChars:40000});
await fs.writeFile(path.join(root,'.pptx-build','source-inspect.ndjson'),snap.ndjson);
for(let i=0;i<ppt.slides.items.length;i++){
  const blob=await ppt.export({slide:ppt.slides.items[i],format:'png',scale:1});
  await fs.writeFile(path.join(root,'.pptx-build',`source-slide-${String(i+1).padStart(2,'0')}.png`),new Uint8Array(await blob.arrayBuffer()));
}
console.log('done');
