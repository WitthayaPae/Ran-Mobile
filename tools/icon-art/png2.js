const fs=require('fs'),zlib=require('zlib');
function decode(file){
  const b=fs.readFileSync(file);
  let p=8,w=0,h=0,bd=8,ct=6,il=0;const idat=[];
  while(p<b.length){const l=b.readUInt32BE(p),t=b.toString('latin1',p+4,p+8);
    if(t==='IHDR'){w=b.readUInt32BE(p+8);h=b.readUInt32BE(p+12);bd=b[p+16];ct=b[p+17];il=b[p+20];}
    else if(t==='IDAT')idat.push(b.slice(p+8,p+8+l));
    p+=12+l;}
  if(il) throw new Error('interlaced');
  const ch=ct===6?4:ct===2?3:ct===4?2:1;
  const raw=zlib.inflateSync(Buffer.concat(idat));
  const st=w*ch, out=Buffer.alloc(w*h*4);
  let prev=Buffer.alloc(st);
  for(let y=0;y<h;y++){
    const ft=raw[y*(st+1)];
    const r=Buffer.from(raw.slice(y*(st+1)+1,(y+1)*(st+1)));
    for(let i=0;i<st;i++){
      const a=i>=ch?r[i-ch]:0,bb=prev[i],c=i>=ch?prev[i-ch]:0;let v=r[i];
      if(ft===1)v+=a;else if(ft===2)v+=bb;else if(ft===3)v+=(a+bb)>>1;
      else if(ft===4){const pp=a+bb-c,pa=Math.abs(pp-a),pb=Math.abs(pp-bb),pc=Math.abs(pp-c);
        v+=(pa<=pb&&pa<=pc)?a:(pb<=pc?bb:c);}
      r[i]=v&255;}
    for(let x=0;x<w;x++){
      const s=x*ch,d=(y*w+x)*4;
      if(ch>=3){out[d]=r[s];out[d+1]=r[s+1];out[d+2]=r[s+2];out[d+3]=ch===4?r[s+3]:255;}
      else {out[d]=out[d+1]=out[d+2]=r[s];out[d+3]=ch===2?r[s+1]:255;}
    }
    prev=r;}
  return {w,h,d:out,hasAlpha:ch===4||ch===2};
}
module.exports={decode};
