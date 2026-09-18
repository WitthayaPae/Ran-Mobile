const {decode}=require('./png2.js');
//  Area-average downsample, done on premultiplied colour so transparent pixels
//  cannot bleed their colour into the edge.
function resize(img,W,H){
  const out=Buffer.alloc(W*H*4);
  const sx=img.w/W, sy=img.h/H;
  for(let y=0;y<H;y++){
    const y0=Math.floor(y*sy), y1=Math.min(img.h,Math.ceil((y+1)*sy));
    for(let x=0;x<W;x++){
      const x0=Math.floor(x*sx), x1=Math.min(img.w,Math.ceil((x+1)*sx));
      let r=0,g=0,b=0,a=0,n=0;
      for(let j=y0;j<y1;j++)for(let i=x0;i<x1;i++){
        const k=(j*img.w+i)*4, al=img.d[k+3]/255;
        r+=img.d[k]*al; g+=img.d[k+1]*al; b+=img.d[k+2]*al; a+=al; n++;
      }
      const d=(y*W+x)*4;
      if(n===0||a===0){out[d]=out[d+1]=out[d+2]=out[d+3]=0;continue;}
      out[d]=Math.min(255,Math.round(r/a));
      out[d+1]=Math.min(255,Math.round(g/a));
      out[d+2]=Math.min(255,Math.round(b/a));
      out[d+3]=Math.round(255*a/n);
    }
  }
  return out;
}
module.exports={resize};
