//  The iOS app icons, from the same mark the Android icons were cut from.
//
//  Usage: node MOBILE/tools/rcc-extract/make-ios-icons.js [mark.png]
//
//  Default source is MOBILE/native/android/res/drawable-nodpi/ran_mark.png —
//  the 512px transparent master make-icons.js writes. iOS icons must be fully
//  opaque with no alpha channel at all (a transparent icon is rejected), so the
//  mark is composited onto the same black field the Android launcher icon uses.
//
//  Loose PNGs plus CFBundleIconFiles, not an asset catalog: a catalog has to be
//  compiled by actool, which is Xcode-only, and this has to run on the machine
//  the art is on. That is enough for development and TestFlight builds. An App
//  Store submission additionally wants a 1024px marketing icon in a catalog,
//  and 1024 is an upscale from this 512 master — ship the original artwork at
//  1024 before submitting.
const path = require('path');
const u = require(path.join(__dirname, 'imgutil.js'));

const SRC = process.argv[2] ||
    'MOBILE/native/android/res/drawable-nodpi/ran_mark.png';
const OUT = 'MOBILE/native/platform/ios/icons';

require('fs').mkdirSync(OUT, { recursive: true });
const mark = u.decode(SRC);
console.log('source ' + SRC + '  ' + mark.w + 'x' + mark.h);

//  Flattened onto black, alpha dropped. The black is part of the artwork on
//  Android too — the logo reads as a coin on it.
function onBlack(size) {
  const s = u.resize(mark, size, size);
  const out = Buffer.alloc(size * size * 4);
  for (let i = 0; i < size * size; i++) {
    const a = s.px[i * 4 + 3] / 255;
    out[i * 4]     = Math.round(s.px[i * 4]     * a);
    out[i * 4 + 1] = Math.round(s.px[i * 4 + 1] * a);
    out[i * 4 + 2] = Math.round(s.px[i * 4 + 2] * a);
    out[i * 4 + 3] = 255;
  }
  return { w: size, h: size, px: out };
}

/*  Every size iOS asks a phone or tablet for, in pixels.

    20/29/40 are settings, notifications and Spotlight; 60 is the iPhone home
    screen and 76/83.5 the iPad's. Named by pixel size because that is what
    CFBundleIconFiles matches on: iOS appends @2x/@3x itself only for a catalog,
    so with loose files each size is its own file and the list names them all. */
const sizes = [20, 29, 40, 58, 60, 76, 80, 87, 120, 152, 167, 180, 512];

for (const s of sizes) {
  if (s > mark.w)
    console.log('  ' + s + 'px is an upscale from the ' + mark.w + 'px master');
  u.write(path.join(OUT, 'AppIcon-' + s + '.png'), onBlack(s));
  console.log('  wrote AppIcon-' + s + '.png');
}
console.log('\n' + sizes.length + ' icons in ' + OUT);
