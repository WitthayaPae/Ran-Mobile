'use strict';
//
// The client's own configuration, from `param.ini` / `config.ini` / `option.ini`.
//
//   node extract-config.js
//   node extract-config.js --out MOBILE/assets/clientconfig.json
//
// The PC client does not ask anyone for a server address. It reads
// `[SERVER SET] LoginAddress` and `nLoginPort` out of `param.ini` at startup,
// connects there, and the LOGIN SERVER returns the list of game servers to
// choose from. The mobile client has no business behaving differently — a typed
// address is a worse version of a solved problem, and it invites typos into the
// one step where a mistake looks identical to the server being down.
//
// All three .ini files ship ENCRYPTED with the same scheme as the rest of the
// client data, which is why they look like binary garbage in a text editor;
// `gamecrypt.decode` is the same routine the archives use.
const fs = require('fs');
const path = require('path');
const G = require('./gamecrypt.js');

const base = path.resolve(__dirname, '../../..');
const CLIENT = path.join(base, 'CLIENT');

/** Parse a decoded .ini into { section: { key: value } }. */
function parseIni(text) {
  const out = {};
  let section = '';
  for (const raw of text.split(/\r?\n/)) {
    const line = raw.trim();
    if (!line || line.startsWith(';')) continue;
    const sec = /^\[(.+)\]$/.exec(line);
    if (sec) { section = sec[1].trim(); out[section] = out[section] || {}; continue; }
    const eq = line.indexOf('=');
    if (eq < 0) continue;
    const k = line.slice(0, eq).trim();
    const v = line.slice(eq + 1).trim();
    if (!k) continue;
    (out[section] = out[section] || {})[k] = v;
  }
  return out;
}

function readIni(name) {
  const p = path.join(CLIENT, name);
  if (!fs.existsSync(p)) return null;
  const raw = fs.readFileSync(p);
  // Encoded is the norm, but a plain file must still work — an operator may
  // have replaced one by hand, and failing on that would be gratuitous.
  const buf = G.isEncoded(raw) ? G.decode(raw) : raw;
  return parseIni(buf.toString('latin1').replace(/[^\x20-\x7e\r\n]/g, ''));
}

const param = readIni('param.ini');
if (!param) throw new Error('CLIENT/param.ini not found');
const config = readIni('config.ini') || {};
const option = readIni('option.ini') || {};

const server = param['SERVER SET'] || {};
const login = {
  host: server.LoginAddress || '',
  port: parseInt(server.nLoginPort, 10) || 0,
  serverName: server.ServerName || '',
};

if (!login.host || !login.port) {
  console.log('WARNING: no LoginAddress/nLoginPort in param.ini [SERVER SET]');
}

const out = {
  login,
  // Carried through so the client can show the same window title and language
  // the PC build does rather than inventing its own.
  title: (param['ETC OPTION'] || {}).ClientWindowTitle
      || (config['GAME_FEATURE'] || {}).szClientWindowTitle || 'Ran Online',
  country: (param['TEXT FILE'] || {}).strCountry || '',
  langSet: parseInt((param['GUI OPTION'] || {}).dwLangSet, 10) || 0,
  version: parseInt((config['GAME_FEATURE'] || {}).dwVersion, 10) || 0,
  region: parseInt(server.nChinaRegion, 10) || 0,
  screen: {
    width: parseInt((option['SCREEN OPTION'] || {}).dwScrWidth, 10) || 0,
    height: parseInt((option['SCREEN OPTION'] || {}).dwScrHeight, 10) || 0,
  },
};

console.log(`login server: ${out.login.host}:${out.login.port}` +
            (out.login.serverName ? `  (${out.login.serverName})` : ''));
console.log(`title "${out.title}"  country ${out.country}  ` +
            `lang ${out.langSet}  version ${out.version}`);

const at = process.argv.indexOf('--out');
if (at > 0) {
  const p = process.argv[at + 1];
  fs.writeFileSync(path.join(base, p), JSON.stringify(out));
  console.log(`wrote ${p}`);
}

module.exports = { parseIni, readIni };
