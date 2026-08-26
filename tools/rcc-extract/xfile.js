'use strict';
//
// DirectX .x container reader.
//
// The 16-byte header is `xof ` + major/minor + format + float size, e.g.
// "xof 0303bin 0032". Three formats ship in Ran/:
//
//   bin   3,225 files   binary token stream
//   bzip    935 files   MSZip-compressed binary
//   txt     387 files   text token stream
//
// Decompressing bzip yields the same binary stream, so one binary tokenizer
// covers 4,160 of 4,567 files and the text tokenizer covers the rest.
//
const zlib = require('zlib');

// ---------------------------------------------------------------------------
// MSZip
//
// Not a standard zlib container. Layout is:
//   u32  total uncompressed size
//   then repeating blocks:
//     u16 uncompressed block size (<= 32768)
//     u16 compressed block size
//     'CK'
//     raw deflate data
//
// Each block after the first is deflated against the PREVIOUS block's output as
// a preset dictionary — that is what "invalid distance too far back" means if
// you try to inflate a later block standalone.
// ---------------------------------------------------------------------------
/**
 * Inflate a raw-deflate chunk that back-references up to 32K of prior output.
 *
 * zlib's `dictionary` option is the obvious tool and it does not work reliably
 * here — some blocks fail with "invalid stored block lengths" even when the
 * history is provably correct (every block in the file declares 32768 bytes,
 * so there is no short-block edge case). Instead, prepend the history to the
 * stream as an uncompressed *stored* deflate block and drop it from the
 * output: the back-references then resolve against real stream data and no
 * dictionary API is involved.
 *
 * Stored block layout: one byte (BFINAL=0, BTYPE=00), u16 LEN, u16 ~LEN, bytes.
 */
function inflateWithHistory(chunk, history) {
  if (!history || history.length === 0) return zlib.inflateRawSync(chunk);

  const len = history.length; // <= 32768, fits a single stored block
  const header = Buffer.alloc(5);
  header[0] = 0x00;
  header.writeUInt16LE(len, 1);
  header.writeUInt16LE(~len & 0xffff, 3);

  const out = zlib.inflateRawSync(Buffer.concat([header, history, chunk]));
  return out.subarray(len);
}

/**
 * @param {Buffer} buf
 * @param {number} offset
 * @param {{tolerant?:boolean}} [opts] when tolerant, a block that fails to
 *   inflate ends the stream and whatever decoded so far is returned, with
 *   `truncated` set. 14 of the 935 compressed meshes in Ran/ have a stream that
 *   stops being self-consistent partway through (verified: every preceding
 *   block declares and produces exactly 32768 bytes, so the history is correct,
 *   and no byte offset in +/-8 recovers it). Losing the whole mesh over a bad
 *   tail is worse than importing the part that decodes.
 */
function decompressMSZip(buf, offset, opts = {}) {
  const total = buf.readUInt32LE(offset);
  const parts = [];
  let off = offset + 4;
  let produced = 0;
  let dictionary = null;
  let truncated = false;

  while (produced < total && off + 6 <= buf.length) {
    const uncompressed = buf.readUInt16LE(off);
    const compressed = buf.readUInt16LE(off + 2);
    if (buf.toString('latin1', off + 4, off + 6) !== 'CK') {
      throw new Error(`MSZip: missing CK signature at ${off + 4}`);
    }
    const start = off + 6;
    const end = start + compressed - 2; // the 'CK' counts toward compressed size
    const chunk = buf.subarray(start, Math.min(end, buf.length));

    let out;
    try {
      out = inflateWithHistory(chunk, dictionary);
    } catch (err) {
      if (!opts.tolerant) throw err;
      truncated = true;
      break;
    }
    parts.push(out);
    produced += out.length;
    // The deflate history is the last 32K of the UNCOMPRESSED STREAM, which is
    // not the same as the previous block's output: blocks are usually 32768
    // bytes but not always, and once one is short the window spans two blocks.
    // Using just the previous block there desyncs the stream several blocks
    // later with a misleading "invalid stored block lengths".
    if (out.length >= 32768) {
      dictionary = out.subarray(out.length - 32768);
    } else {
      const tail = Buffer.concat(dictionary ? [dictionary, out] : [out]);
      dictionary = tail.length > 32768 ? tail.subarray(tail.length - 32768) : tail;
    }
    off = end;
    if (uncompressed === 0) break;
  }

  const all = Buffer.concat(parts);
  const body = all.length > total ? all.subarray(0, total) : all;
  body.truncated = truncated;
  return body;
}

/**
 * Read a .x file and return its uncompressed body plus format.
 * @returns {{format:'bin'|'txt', floatBits:number, body:Buffer}}
 */
function open(buf, opts = {}) {
  if (buf.length < 16 || buf.toString('latin1', 0, 4) !== 'xof ') {
    throw new Error('not a DirectX .x file');
  }
  const format = buf.toString('latin1', 8, 12).trim();
  const floatBits = parseInt(buf.toString('latin1', 12, 16), 10);

  if (format === 'txt') return { format: 'txt', floatBits, body: buf.subarray(16) };
  if (format === 'bin') return { format: 'bin', floatBits, body: buf.subarray(16) };
  if (format === 'bzip' || format === 'tzip') {
    const body = decompressMSZip(buf, 16, opts);
    return { format: format === 'bzip' ? 'bin' : 'txt', floatBits, body,
             truncated: !!body.truncated };
  }
  throw new Error(`unsupported .x format "${format}"`);
}

// ---------------------------------------------------------------------------
// Binary tokens (DirectX .x spec)
// ---------------------------------------------------------------------------
const TOKEN = {
  NAME: 1, STRING: 2, INTEGER: 3, GUID: 5,
  INTEGER_LIST: 6, FLOAT_LIST: 7,
  OBRACE: 10, CBRACE: 11, OPAREN: 12, CPAREN: 13,
  OBRACKET: 14, CBRACKET: 15, OANGLE: 16, CANGLE: 17,
  DOT: 18, COMMA: 19, SEMICOLON: 20,
  TEMPLATE: 31, WORD: 40, DWORD: 41, FLOAT: 42, DOUBLE: 43,
  CHAR: 44, UCHAR: 45, SWORD: 46, SDWORD: 47, VOID: 48,
  LPSTR: 49, UNICODE: 50, CSTRING: 51, ARRAY: 52,
};

/**
 * Tokenize a binary .x body.
 * Emits { type, value } records; INTEGER_LIST/FLOAT_LIST carry typed arrays.
 */
function* binaryTokens(body, floatBits, opts = {}) {
  let p = 0;
  // On a recovered (truncated) body the stream simply ends mid-token. That is
  // expected, not an error, so stop cleanly and let the caller use what parsed.
  class EndOfStream extends Error {}
  const need = (n) => {
    if (p + n > body.length) {
      if (opts.tolerant) throw new EndOfStream();
      throw new Error('truncated .x');
    }
  };
  try {
    yield* tokens();
  } catch (err) {
    if (!(err instanceof EndOfStream)) throw err;
  }
  return;

  function* tokens() {

  while (p + 2 <= body.length) {
    const t = body.readUInt16LE(p);
    p += 2;
    switch (t) {
      case TOKEN.NAME: {
        need(4);
        const n = body.readUInt32LE(p); p += 4;
        need(n);
        const s = body.toString('latin1', p, p + n); p += n;
        yield { type: 'NAME', value: s };
        break;
      }
      case TOKEN.STRING: {
        need(4);
        const n = body.readUInt32LE(p); p += 4;
        need(n);
        const s = body.toString('latin1', p, p + n); p += n;
        p += 2; // trailing separator token (semicolon or comma)
        yield { type: 'STRING', value: s };
        break;
      }
      case TOKEN.INTEGER: {
        need(4);
        yield { type: 'INTEGER', value: body.readUInt32LE(p) };
        p += 4;
        break;
      }
      case TOKEN.GUID: {
        need(16);
        yield { type: 'GUID', value: body.toString('hex', p, p + 16) };
        p += 16;
        break;
      }
      case TOKEN.INTEGER_LIST: {
        need(4);
        const n = body.readUInt32LE(p); p += 4;
        need(n * 4);
        const arr = new Uint32Array(n);
        for (let i = 0; i < n; i++) arr[i] = body.readUInt32LE(p + i * 4);
        p += n * 4;
        yield { type: 'INTEGER_LIST', value: arr };
        break;
      }
      case TOKEN.FLOAT_LIST: {
        need(4);
        const n = body.readUInt32LE(p); p += 4;
        const size = floatBits === 64 ? 8 : 4;
        need(n * size);
        const arr = new Float64Array(n);
        for (let i = 0; i < n; i++) {
          arr[i] = size === 8 ? body.readDoubleLE(p + i * 8) : body.readFloatLE(p + i * 4);
        }
        p += n * size;
        yield { type: 'FLOAT_LIST', value: arr };
        break;
      }
      case TOKEN.OBRACE: yield { type: '{' }; break;
      case TOKEN.CBRACE: yield { type: '}' }; break;
      case TOKEN.OPAREN: yield { type: '(' }; break;
      case TOKEN.CPAREN: yield { type: ')' }; break;
      case TOKEN.OBRACKET: yield { type: '[' }; break;
      case TOKEN.CBRACKET: yield { type: ']' }; break;
      case TOKEN.OANGLE: yield { type: '<' }; break;
      case TOKEN.CANGLE: yield { type: '>' }; break;
      case TOKEN.DOT: yield { type: '.' }; break;
      case TOKEN.COMMA: yield { type: ',' }; break;
      case TOKEN.SEMICOLON: yield { type: ';' }; break;
      case TOKEN.TEMPLATE: yield { type: 'TEMPLATE' }; break;
      default:
        // Primitive type keywords inside template definitions.
        yield { type: 'KEYWORD', value: t };
        break;
    }
  }
  }
}

module.exports = { open, decompressMSZip, binaryTokens, TOKEN };
