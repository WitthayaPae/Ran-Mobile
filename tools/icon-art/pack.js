'use strict';
//  Build mobile_icons.dds from the delivered artwork.
//
//  The cell layout is unchanged, so every control already pointed at this
//  atlas keeps working without touching the XML again. Cells with nothing
//  delivered keep the drawn version that is already there.
const fs=require('fs'), path=require('path');
const {decode}=require('./png2.js'), {resize}=require('./resize.js');
const png=require('C:/Users/tapnu/Downloads/RAN/DEV EP9/MOBILE/tools/rcc-extract/png.js');
const SRC='C:/Users/tapnu/Downloads/RanIcon/';

//  index -> [delivered file | null, fallback drawn file]
const CELLS=[
 ['inventory.png','new-inventory.png'], ['character.png','new-character.png'],
 ['skill.png','new-skill.png'],         ['party.png','new-party.png'],
 ['guild.png','new-guild.png'],         ['quest.png','new-quest.png'],
 ['friend.png','new-friend.png'],       ['largemap.png','new-largemap.png'],
 ['chatmacro.png','new-chatmacro.png'], ['itembank.png','new-itembank.png'],
 ['itemshop.png','new-itemshop.png'],   [null,'new-run.png'],
 ['finder.png','new-finder.png'],       ['ranking.png','new-ranking.png'],
 ['competition.png','new-competition.png'], ['bossviewer.png','new-bossviewer.png'],
 ['auction.png','new-auction.png'],     ['itemmall.png','new-itemmall.png'],
 ['product.png','new-product.png'],     ['escmenu.png','new-escmenu.png'],
 [null,'new-run_on.png'],               ['press.png','new-over.png'],
 [null,'new-alert.png'],                ['qbox.png',null],
 ['miniparty.png',null],
];

//  Cut a flat light background where the generator gave us no alpha.
//
//  A flood fill from the four corners, not a colour key: several of these
//  icons have their own light greys in the artwork, and keying the colour
//  would punch holes through the middle of them.
//  Borrow the plate silhouette from the icons that came back with alpha.
//
//  Three of the menu icons arrived as flat RGB on a light grey gradient, and a
//  flood fill made a poor job of it: the background runs from about 200 at the
//  corners to 140 at the middle of an edge, and any tolerance loose enough to
//  walk that also starts eating the icon.
//
//  But every menu icon is the same rounded plate at the same place - that is
//  what the brief asked for - so the shape is already known from the ones that
//  do have alpha. Taking the median of several of those gives a clean mask,
//  and it is exact rather than approximate.
let PLATE=null;
function plateMask(size){
  if(PLATE) return PLATE;
  const donors=['inventory.png','quest.png','guild.png','auction.png','product.png'];
  const got=[];
  for(const d of donors){
    if(!fs.existsSync(SRC+d)) continue;
    const im=decode(SRC+d);
    if(!im.hasAlpha) continue;
    got.push(resize(im,size,size));
  }
  const out=Buffer.alloc(size*size);
  for(let k=0;k<size*size;k++){
    const v=got.map(g=>g[k*4+3]).sort((a,b)=>a-b);
    out[k]=v.length?v[v.length>>1]:255;
  }
  PLATE=out;
  console.log('   plate mask built from '+got.length+' icons');
  return PLATE;
}

//  Push the edge colour outward into the transparent texels.
//
//  A transparent pixel still has a colour, and the sampler mixes it in at the
//  edges when the icon is scaled - which fringes the outline with whatever
//  happened to be in the background of the source file. Carrying the nearest
//  opaque colour outward a few pixels means the mix has nothing wrong to find.
function bleed(px,size,passes){
  for(let p=0;p<passes;p++){
    const src=Buffer.from(px);
    for(let y=0;y<size;y++)for(let x=0;x<size;x++){
      const k=(y*size+x)*4;
      if(src[k+3]>0)continue;
      let r=0,g=0,b=0,n=0;
      for(let dy=-1;dy<=1;dy++)for(let dx=-1;dx<=1;dx++){
        const nx=x+dx, ny=y+dy;
        if(nx<0||ny<0||nx>=size||ny>=size)continue;
        const m=(ny*size+nx)*4;
        if(src[m+3]===0)continue;
        r+=src[m];g+=src[m+1];b+=src[m+2];n++;
      }
      if(!n)continue;
      px[k]=Math.round(r/n); px[k+1]=Math.round(g/n); px[k+2]=Math.round(b/n);
    }
  }
}

const N=128, COLS=8, AW=1024, AH=512;
const atlas=Buffer.alloc(AW*AH*4);
let used=0, cut=0;

CELLS.forEach((pair,n)=>{
  const [give,fall]=pair;
  let file=null, needCut=false;
  if(give && fs.existsSync(SRC+give)){ file=SRC+give; needCut=true; }
  else if(fall && fs.existsSync(fall)) file=fall;
  if(!file){ console.log(String(n).padStart(2)+'  (empty)'); return; }

  let img=decode(file);
  const px=resize(img,N,N);
  let frac=0;
  if(needCut && !img.hasAlpha){
    const m=plateMask(N);
    for(let k=0;k<N*N;k++) px[k*4+3]=m[k];
    frac=1; cut++;
  }
  bleed(px,N,4);
  const ox=(n%COLS)*N, oy=Math.floor(n/COLS)*N;
  for(let y=0;y<N;y++)
    px.copy(atlas,((oy+y)*AW+ox)*4, y*N*4, (y+1)*N*4);
  used++;
  console.log(String(n).padStart(2)+'  '+path.basename(file).padEnd(20)+' -> '+ox+','+oy+(frac?'  [plate mask applied]':''));
});

fs.writeFileSync('atlas.png',png.encode(AW,AH,atlas));
console.log('\n'+used+' cells filled, '+cut+' backgrounds cut -> atlas.png');
