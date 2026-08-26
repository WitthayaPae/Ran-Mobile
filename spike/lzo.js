'use strict';
//
// LZO1X-1 decompressor.
//
// The server compresses with lzo1x_1_compress and decompresses with
// lzo1x_decompress_safe (SOURCE/Lib_Network/MinLzo.cpp:112,158). The stream is
// raw LZO1X — no magic, no length prefix, no framing of its own. The output
// size is not transmitted; the caller supplies a capacity, exactly as the
// server does (RcvMsgBuffer.cpp:113 passes NET_DATA_BUFSIZE).
//
// This is a direct transcription of the reference lzo1x_decompress state
// machine. The original is written with gotos; the states below carry the same
// names so it can be diffed against the reference by eye.
//
// Decompression only. The client never needs to compress: CNetClient sends
// plain messages and only the server batches.
//

const M2_MAX_OFFSET = 0x0800;

function lzo1xDecompress(src, maxOut = 2048) {
  const out = Buffer.alloc(maxOut);
  let ip = 0;
  let op = 0;
  let t = 0;
  let m = 0;

  const need = (n) => {
    if (ip + n > src.length) throw new Error(`lzo: input overrun at ${ip}`);
  };
  const put = (b) => {
    if (op >= out.length) throw new Error(`lzo: output overrun at ${op}`);
    out[op++] = b;
  };
  const backref = () => {
    if (m < 0 || m >= op) throw new Error(`lzo: bad back-reference ${m} (op=${op})`);
  };
  const copyLiteral = (n) => { need(n); for (let i = 0; i < n; i++) put(src[ip++]); };
  // Matches may overlap the output cursor (run-length style), so this must copy
  // byte at a time rather than using a block move.
  const copyMatch = (n) => { for (let i = 0; i < n; i++) { backref(); put(out[m++]); } };
  const readLongLength = (base) => {
    need(1);
    while (src[ip] === 0) { t += 255; ip++; need(1); }
    t += base + src[ip++];
  };

  let state;
  need(1);
  if (src[ip] > 17) {
    t = src[ip++] - 17;
    if (t < 4) {
      state = 'MATCH_NEXT';
    } else {
      copyLiteral(t);
      state = 'FIRST_LITERAL_RUN';
    }
  } else {
    state = 'TOP';
  }

  for (;;) {
    switch (state) {
      case 'TOP': {
        need(1);
        t = src[ip++];
        if (t >= 16) { state = 'MATCH'; break; }
        if (t === 0) readLongLength(15);
        copyLiteral(t + 3);
        state = 'FIRST_LITERAL_RUN';
        break;
      }

      case 'FIRST_LITERAL_RUN': {
        need(1);
        t = src[ip++];
        if (t >= 16) { state = 'MATCH'; break; }
        need(1);
        m = op - (1 + M2_MAX_OFFSET) - (t >> 2) - (src[ip++] << 2);
        backref(); put(out[m++]);
        backref(); put(out[m++]);
        backref(); put(out[m]);
        state = 'MATCH_DONE';
        break;
      }

      case 'MATCH': {
        if (t >= 64) {
          need(1);
          m = op - 1 - ((t >> 2) & 7) - (src[ip++] << 3);
          t = (t >> 5) - 1;
          state = 'COPY_MATCH';
        } else if (t >= 32) {
          t &= 31;
          if (t === 0) readLongLength(31);
          need(2);
          m = op - 1 - ((src[ip] >> 2) + (src[ip + 1] << 6));
          ip += 2;
          state = 'COPY_MATCH';
        } else if (t >= 16) {
          m = op - ((t & 8) << 11);
          t &= 7;
          if (t === 0) readLongLength(7);
          need(2);
          m -= (src[ip] >> 2) + (src[ip + 1] << 6);
          ip += 2;
          // The only clean termination: a match that points at the cursor.
          if (m === op) return out.subarray(0, op);
          m -= 0x4000;
          state = 'COPY_MATCH';
        } else {
          need(1);
          m = op - 1 - (t >> 2) - (src[ip++] << 2);
          backref(); put(out[m++]);
          backref(); put(out[m]);
          state = 'MATCH_DONE';
        }
        break;
      }

      case 'COPY_MATCH': {
        backref(); put(out[m++]);
        backref(); put(out[m++]);
        copyMatch(t);
        state = 'MATCH_DONE';
        break;
      }

      case 'MATCH_DONE': {
        // The low 2 bits of the control byte carry a 0-3 byte literal run.
        t = src[ip - 2] & 3;
        state = t === 0 ? 'TOP' : 'MATCH_NEXT';
        break;
      }

      case 'MATCH_NEXT': {
        copyLiteral(t);
        need(1);
        t = src[ip++];
        state = 'MATCH';
        break;
      }

      default:
        throw new Error(`lzo: bad state ${state}`);
    }
  }
}

module.exports = { lzo1xDecompress };
