const DEBUG_USAGE_PAGE = 0xff00;
const DEBUG_USAGE = 0x01;
const DEFAULT_VENDOR_ID = 0xcafe;
const DEFAULT_PRODUCT_ID = 0x4004;
const PACKET_SIZE = 64;
const INIT_PAYLOAD_SIZE = 57;
const CONT_PAYLOAD_SIZE = 59;
const BROADCAST_CID = 0xffffffff;
const COMMANDS = {
  ping: 0x01,
  init: 0x06,
  cbor: 0x10,
  error: 0x3f,
};
const ERROR_CODES = {
  0x01: "INVALID_CMD",
  0x03: "INVALID_LEN",
  0x04: "INVALID_SEQ",
  0x0b: "INVALID_CID",
};
const CTAP_STATUS_CODES = {
  0x00: "OK",
  0x12: "INVALID_CBOR",
  0x14: "MISSING_PARAMETER",
  0x19: "CREDENTIAL_EXCLUDED",
  0x22: "INVALID_CREDENTIAL",
  0x26: "UNSUPPORTED_ALGORITHM",
  0x28: "KEY_STORE_FULL",
  0x2b: "UNSUPPORTED_OPTION",
  0x2e: "NO_CREDENTIALS",
};
const STORAGE_KEY = "meowkey-console-state";

const elements = {
  connectButton: document.querySelector("#connect-button"),
  reconnectButton: document.querySelector("#reconnect-button"),
  disconnectButton: document.querySelector("#disconnect-button"),
  initButton: document.querySelector("#init-button"),
  pingButton: document.querySelector("#ping-button"),
  getInfoButton: document.querySelector("#get-info-button"),
  makeCredentialButton: document.querySelector("#make-credential-button"),
  getAssertionButton: document.querySelector("#get-assertion-button"),
  clearLogButton: document.querySelector("#clear-log-button"),
  pingInput: document.querySelector("#ping-input"),
  rpIdInput: document.querySelector("#rp-id-input"),
  userIdInput: document.querySelector("#user-id-input"),
  userNameInput: document.querySelector("#user-name-input"),
  displayNameInput: document.querySelector("#display-name-input"),
  credentialIdInput: document.querySelector("#credential-id-input"),
  excludeListInput: document.querySelector("#exclude-list-input"),
  statusBadge: document.querySelector("#status-badge"),
  deviceName: document.querySelector("#device-name"),
  deviceId: document.querySelector("#device-id"),
  channelId: document.querySelector("#channel-id"),
  capabilities: document.querySelector("#capabilities"),
  initOutput: document.querySelector("#init-output"),
  pingOutput: document.querySelector("#ping-output"),
  infoOutput: document.querySelector("#info-output"),
  makeCredentialOutput: document.querySelector("#make-credential-output"),
  getAssertionOutput: document.querySelector("#get-assertion-output"),
  logOutput: document.querySelector("#log-output"),
  logTemplate: document.querySelector("#log-entry-template"),
};

const state = {
  device: null,
  channelId: null,
  pending: null,
  capabilities: [],
  lastCredentialIdHex: "",
};

function setBadge(text) {
  elements.statusBadge.textContent = text;
}

function setText(element, value) {
  element.textContent = value;
}

function formatHex(bytes) {
  return Array.from(bytes, (value) => value.toString(16).padStart(2, "0")).join(" ");
}

function bytesToHexCompact(bytes) {
  return Array.from(bytes, (value) => value.toString(16).padStart(2, "0")).join("");
}

function hexToBytes(text) {
  const normalized = text.replace(/\s+/g, "").trim().toLowerCase();
  if (!normalized) {
    return new Uint8Array();
  }
  if (!/^[0-9a-f]+$/.test(normalized) || (normalized.length % 2) !== 0) {
    throw new Error("凭据 ID 必须是偶数长度的十六进制字符串。");
  }
  return Uint8Array.from({ length: normalized.length / 2 }, (_, index) =>
    Number.parseInt(normalized.slice(index * 2, index * 2 + 2), 16)
  );
}

function parseExcludeList(text) {
  return text
    .split(/\r?\n/)
    .map((line) => line.trim())
    .filter(Boolean)
    .map((line) => ({ type: "public-key", id: hexToBytes(line) }));
}

function utf8Encode(text) {
  return new TextEncoder().encode(text);
}

function utf8Decode(bytes) {
  return new TextDecoder().decode(bytes);
}

function ctapStatusName(status) {
  if (typeof status !== "number") {
    return "缺少状态码";
  }
  return CTAP_STATUS_CODES[status] || `0x${status.toString(16).padStart(2, "0")}`;
}

function readStatusByte(payload, commandName) {
  const status = payload[0];
  if (typeof status !== "number") {
    throw new Error(`${commandName} 返回了空的 CTAP 响应负载。`);
  }
  return status;
}

function saveUiState() {
  const snapshot = {
    rpId: elements.rpIdInput?.value ?? "",
    userId: elements.userIdInput?.value ?? "",
    userName: elements.userNameInput?.value ?? "",
    displayName: elements.displayNameInput?.value ?? "",
    credentialId: elements.credentialIdInput?.value ?? "",
    excludeList: elements.excludeListInput?.value ?? "",
  };
  localStorage.setItem(STORAGE_KEY, JSON.stringify(snapshot));
}

function loadUiState() {
  const raw = localStorage.getItem(STORAGE_KEY);
  if (!raw) {
    return;
  }

  try {
    const snapshot = JSON.parse(raw);
    elements.rpIdInput.value = snapshot.rpId || elements.rpIdInput.value;
    elements.userIdInput.value = snapshot.userId || elements.userIdInput.value;
    elements.userNameInput.value = snapshot.userName || elements.userNameInput.value;
    elements.displayNameInput.value = snapshot.displayName || elements.displayNameInput.value;
    elements.credentialIdInput.value = snapshot.credentialId || "";
    elements.excludeListInput.value = snapshot.excludeList || "";
    state.lastCredentialIdHex = elements.credentialIdInput.value.trim();
  } catch {
    logEntry("错误", "本地保存的界面状态已损坏，已忽略。");
  }
}

function makePacket(cid, command, payload) {
  const packet = new Uint8Array(PACKET_SIZE);
  packet[0] = (cid >>> 24) & 0xff;
  packet[1] = (cid >>> 16) & 0xff;
  packet[2] = (cid >>> 8) & 0xff;
  packet[3] = cid & 0xff;
  packet[4] = 0x80 | command;
  packet[5] = (payload.length >>> 8) & 0xff;
  packet[6] = payload.length & 0xff;
  packet.set(payload, 7);
  return packet;
}

function makeInitPacket(cid, command, totalLength, payload) {
  const packet = new Uint8Array(PACKET_SIZE);
  packet[0] = (cid >>> 24) & 0xff;
  packet[1] = (cid >>> 16) & 0xff;
  packet[2] = (cid >>> 8) & 0xff;
  packet[3] = cid & 0xff;
  packet[4] = 0x80 | command;
  packet[5] = (totalLength >>> 8) & 0xff;
  packet[6] = totalLength & 0xff;
  packet.set(payload, 7);
  return packet;
}

function makeContinuationPacket(cid, sequence, payload) {
  const packet = new Uint8Array(PACKET_SIZE);
  packet[0] = (cid >>> 24) & 0xff;
  packet[1] = (cid >>> 16) & 0xff;
  packet[2] = (cid >>> 8) & 0xff;
  packet[3] = cid & 0xff;
  packet[4] = sequence & 0x7f;
  packet.set(payload, 5);
  return packet;
}

function parsePacket(buffer) {
  const packet = new Uint8Array(buffer);
  const isInit = (packet[4] & 0x80) !== 0;
  return {
    raw: packet,
    isInit,
    cid: ((packet[0] << 24) >>> 0) | (packet[1] << 16) | (packet[2] << 8) | packet[3],
    command: isInit ? (packet[4] & 0x7f) : null,
    sequence: isInit ? null : (packet[4] & 0x7f),
    payloadLength: isInit ? ((packet[5] << 8) | packet[6]) : null,
    payload: isInit ? packet.slice(7) : packet.slice(5),
  };
}

function logEntry(tag, body) {
  const fragment = elements.logTemplate.content.cloneNode(true);
  fragment.querySelector(".log-time").textContent = new Date().toLocaleTimeString("zh-CN");
  fragment.querySelector(".log-tag").textContent = tag;
  fragment.querySelector(".log-body").textContent = body;
  elements.logOutput.prepend(fragment);
}

function updateUi() {
  const connected = Boolean(state.device);
  const initialized = connected && state.channelId !== null;

  elements.disconnectButton.disabled = !connected;
  elements.initButton.disabled = !connected;
  elements.pingButton.disabled = !initialized;
  elements.getInfoButton.disabled = !initialized;
  elements.makeCredentialButton.disabled = !initialized;
  elements.getAssertionButton.disabled = !initialized;

  if (!connected) {
    setBadge("空闲");
    setText(elements.deviceName, "未连接");
    setText(elements.deviceId, "-");
    setText(elements.channelId, "未分配");
    setText(elements.capabilities, "-");
    return;
  }

  const productName = state.device.productName || "未命名 HID 设备";
  setBadge(initialized ? "通道已建立" : "已连接");
  setText(elements.deviceName, productName);
  setText(
    elements.deviceId,
    `0x${state.device.vendorId.toString(16).padStart(4, "0")}:0x${state.device.productId
      .toString(16)
      .padStart(4, "0")}`
  );
  setText(elements.channelId, initialized ? `0x${state.channelId.toString(16).padStart(8, "0")}` : "未分配");
  setText(elements.capabilities, state.capabilities.length ? state.capabilities.join(", ") : "-");
}

function normalize(value) {
  if (value instanceof Uint8Array) {
    return `hex:${bytesToHexCompact(value)}`;
  }
  if (Array.isArray(value)) {
    return value.map(normalize);
  }
  if (value && typeof value === "object") {
    const output = {};
    for (const [key, child] of Object.entries(value)) {
      output[key] = normalize(child);
    }
    return output;
  }
  return value;
}

function concatBytes(...chunks) {
  const totalLength = chunks.reduce((sum, chunk) => sum + chunk.length, 0);
  const output = new Uint8Array(totalLength);
  let offset = 0;
  for (const chunk of chunks) {
    output.set(chunk, offset);
    offset += chunk.length;
  }
  return output;
}

function encodeLength(majorType, value) {
  if (value < 24) {
    return Uint8Array.of((majorType << 5) | value);
  }
  if (value < 0x100) {
    return Uint8Array.of((majorType << 5) | 24, value);
  }
  if (value < 0x10000) {
    return Uint8Array.of((majorType << 5) | 25, (value >> 8) & 0xff, value & 0xff);
  }
  return Uint8Array.of(
    (majorType << 5) | 26,
    (value >>> 24) & 0xff,
    (value >>> 16) & 0xff,
    (value >>> 8) & 0xff,
    value & 0xff
  );
}

function encodeCbor(value) {
  if (value instanceof Uint8Array) {
    return concatBytes(encodeLength(2, value.length), value);
  }

  if (typeof value === "string") {
    const encoded = utf8Encode(value);
    return concatBytes(encodeLength(3, encoded.length), encoded);
  }

  if (typeof value === "number") {
    if (!Number.isInteger(value)) {
      throw new Error("当前 CBOR 编码器只支持整数。");
    }
    if (value >= 0) {
      return encodeLength(0, value);
    }
    return encodeLength(1, -value - 1);
  }

  if (typeof value === "boolean") {
    return Uint8Array.of(value ? 0xf5 : 0xf4);
  }

  if (value === null) {
    return Uint8Array.of(0xf6);
  }

  if (Array.isArray(value)) {
    return concatBytes(encodeLength(4, value.length), ...value.map(encodeCbor));
  }

  if (value instanceof Map) {
    const items = [];
    for (const [key, child] of value.entries()) {
      items.push(encodeCbor(key), encodeCbor(child));
    }
    return concatBytes(encodeLength(5, value.size), ...items);
  }

  if (typeof value === "object") {
    const entries = Object.entries(value);
    const items = [];
    for (const [key, child] of entries) {
      items.push(encodeCbor(key), encodeCbor(child));
    }
    return concatBytes(encodeLength(5, entries.length), ...items);
  }

  throw new Error(`不支持的 CBOR 值类型: ${typeof value}`);
}

function decodeCbor(bytes) {
  if (!bytes.length) {
    return { value: null, bytesRead: 0 };
  }

  let offset = 0;

  function readLength(additionalInfo) {
    if (additionalInfo < 24) {
      return additionalInfo;
    }
    if (additionalInfo === 24) {
      return bytes[offset++];
    }
    if (additionalInfo === 25) {
      const value = (bytes[offset] << 8) | bytes[offset + 1];
      offset += 2;
      return value;
    }
    if (additionalInfo === 26) {
      const value =
        (bytes[offset] * 2 ** 24) +
        (bytes[offset + 1] << 16) +
        (bytes[offset + 2] << 8) +
        bytes[offset + 3];
      offset += 4;
      return value;
    }
    throw new Error(`暂不支持的 CBOR 附加信息 ${additionalInfo}`);
  }

  function readItem() {
    const initial = bytes[offset++];
    const majorType = initial >> 5;
    const additionalInfo = initial & 0x1f;

    if (majorType === 0) {
      return readLength(additionalInfo);
    }

    if (majorType === 1) {
      return -readLength(additionalInfo) - 1;
    }

    if (majorType === 2) {
      const length = readLength(additionalInfo);
      const value = bytes.slice(offset, offset + length);
      offset += length;
      return value;
    }

    if (majorType === 3) {
      const length = readLength(additionalInfo);
      const value = utf8Decode(bytes.slice(offset, offset + length));
      offset += length;
      return value;
    }

    if (majorType === 4) {
      const length = readLength(additionalInfo);
      const value = [];
      for (let index = 0; index < length; index += 1) {
        value.push(readItem());
      }
      return value;
    }

    if (majorType === 5) {
      const length = readLength(additionalInfo);
      const value = {};
      for (let index = 0; index < length; index += 1) {
        const key = readItem();
        value[String(key)] = readItem();
      }
      return value;
    }

    if (majorType === 7) {
      if (additionalInfo === 20) {
        return false;
      }
      if (additionalInfo === 21) {
        return true;
      }
      if (additionalInfo === 22) {
        return null;
      }
    }

    throw new Error(`暂不支持的 CBOR 主类型 ${majorType}`);
  }

  const value = readItem();
  return { value, bytesRead: offset };
}

function decodeCapabilities(mask) {
  const values = [];
  if (mask & 0x04) {
    values.push("支持 CBOR");
  }
  if (mask & 0x08) {
    values.push("无旧版 MSG");
  }
  return values;
}

function parseInitPayload(payload) {
  const nonce = payload.slice(0, 8);
  const channelId =
    ((payload[8] << 24) >>> 0) | (payload[9] << 16) | (payload[10] << 8) | payload[11];
  const capabilities = decodeCapabilities(payload[16] || 0);
  return {
    nonce: bytesToHexCompact(nonce),
    channelId: `0x${channelId.toString(16).padStart(8, "0")}`,
    ctaphidVersion: payload[12],
    deviceVersion: {
      major: payload[13],
      minor: payload[14],
      build: payload[15],
    },
    capabilities,
  };
}

function parseInfoPayload(payload) {
  const status = readStatusByte(payload, "authenticatorGetInfo");
  if (status !== 0x00) {
    return { status, error: `CTAP 返回了非零状态码 ${ctapStatusName(status)}` };
  }

  const decoded = decodeCbor(payload.slice(1));
  return {
    status,
    data: normalize(decoded.value),
  };
}

function extractCredentialIdFromMakeCredential(decoded) {
  const authData = decoded?.["2"];
  if (!(authData instanceof Uint8Array) || authData.length < 55) {
    return null;
  }
  const credentialIdLength = (authData[53] << 8) | authData[54];
  if ((55 + credentialIdLength) > authData.length) {
    return null;
  }
  return authData.slice(55, 55 + credentialIdLength);
}

function commandName(command) {
  if (command === null || command === undefined) {
    return "CONT";
  }
  switch (command) {
    case COMMANDS.ping:
      return "PING";
    case COMMANDS.init:
      return "INIT";
    case COMMANDS.cbor:
      return "CBOR";
    case COMMANDS.error:
      return "ERROR";
    default:
      return `0x${command.toString(16)}`;
  }
}

function rejectPending(error) {
  if (!state.pending) {
    return;
  }

  clearTimeout(state.pending.timeoutId);
  state.pending.reject(error);
  state.pending = null;
}

function resolvePending(packet) {
  if (!state.pending) {
    return false;
  }

  clearTimeout(state.pending.timeoutId);
  state.pending.resolve(packet);
  state.pending = null;
  return true;
}

async function connectDevice(device) {
  if (state.device && state.device !== device && state.device.opened) {
    await state.device.close();
  }

  state.device = device;
  if (!state.device.opened) {
    await state.device.open();
  }

  state.device.removeEventListener("inputreport", handleInputReport);
  state.device.addEventListener("inputreport", handleInputReport);
  state.channelId = null;
  state.capabilities = [];
  state.lastCredentialIdHex = "";
  logEntry("会话", `已连接到 ${state.device.productName || "设备"}`);
  updateUi();
}

async function requestDevice() {
  const devices = await navigator.hid.requestDevice({
    filters: [
      { usagePage: DEBUG_USAGE_PAGE, usage: DEBUG_USAGE },
      { vendorId: DEFAULT_VENDOR_ID, productId: DEFAULT_PRODUCT_ID },
    ],
  });

  if (!devices.length) {
    return;
  }

  await connectDevice(devices[0]);
}

async function reconnectKnown() {
  const devices = await navigator.hid.getDevices();
  const device = devices.find(
    (candidate) =>
      candidate.collections.some(
        (collection) => collection.usagePage === DEBUG_USAGE_PAGE && collection.usage === DEBUG_USAGE
      ) ||
      (candidate.vendorId === DEFAULT_VENDOR_ID && candidate.productId === DEFAULT_PRODUCT_ID)
  );

  if (!device) {
    throw new Error("没有找到此前已授权的 MeowKey 设备。");
  }

  await connectDevice(device);
}

async function disconnect() {
  rejectPending(new Error("设备已断开"));

  if (state.device?.opened) {
    await state.device.close();
  }

  state.device = null;
  state.channelId = null;
  state.capabilities = [];
  state.lastCredentialIdHex = "";
  updateUi();
  logEntry("会话", "连接已断开");
}

async function sendCommand(cid, command, payload) {
  if (!state.device || !state.device.opened) {
    throw new Error("当前没有已连接的 HID 设备。");
  }

  if (state.pending) {
    throw new Error("还有上一条命令正在等待响应。");
  }

  const responsePromise = new Promise((resolve, reject) => {
    const timeoutId = window.setTimeout(() => {
      state.pending = null;
      reject(new Error("等待 CTAPHID 响应超时。"));
    }, 2000);

    state.pending = {
      resolve,
      reject,
      timeoutId,
      cid,
      command,
      totalLength: null,
      receivedLength: 0,
      nextSeq: 0,
      chunks: [],
    };
  });

  {
    const firstChunk = payload.slice(0, INIT_PAYLOAD_SIZE);
    const packet = makeInitPacket(cid, command, payload.length, firstChunk);
    logEntry(
      `发送 ${commandName(command)}`,
      `cid=0x${cid.toString(16).padStart(8, "0")}\nlen=${payload.length}\n${formatHex(packet)}`
    );
    await state.device.sendReport(0, packet);
  }

  {
    let offset = INIT_PAYLOAD_SIZE;
    let sequence = 0;
    while (offset < payload.length) {
      const chunk = payload.slice(offset, offset + CONT_PAYLOAD_SIZE);
      const packet = makeContinuationPacket(cid, sequence, chunk);
      logEntry(
        `发送续包 ${commandName(command)}`,
        `cid=0x${cid.toString(16).padStart(8, "0")}\nseq=${sequence}\n${formatHex(packet)}`
      );
      await state.device.sendReport(0, packet);
      offset += CONT_PAYLOAD_SIZE;
      sequence += 1;
    }
  }

  const response = await responsePromise;

  if (response.command === COMMANDS.error) {
    const errorCode = response.payload[0] ?? 0xff;
    const errorName = ERROR_CODES[errorCode] || "UNKNOWN_ERROR";
    throw new Error(`CTAPHID 错误 ${errorName} (0x${errorCode.toString(16).padStart(2, "0")})`);
  }

  return response;
}

function handleInputReport(event) {
  const packet = parsePacket(event.data.buffer);
  let chunk;

  if (packet.isInit) {
    logEntry(
      `接收 ${commandName(packet.command)}`,
      `cid=0x${packet.cid.toString(16).padStart(8, "0")}\nlen=${packet.payloadLength}\n${formatHex(packet.raw)}`
    );
  } else {
    logEntry(
      "接收续包",
      `cid=0x${packet.cid.toString(16).padStart(8, "0")}\nseq=${packet.sequence}\n${formatHex(packet.raw)}`
    );
  }

  if (!state.pending) {
    return;
  }

  if (packet.isInit) {
    state.pending.cid = packet.cid;
    state.pending.command = packet.command;
    state.pending.totalLength = packet.payloadLength;
    state.pending.receivedLength = Math.min(packet.payload.length, packet.payloadLength);
    state.pending.nextSeq = 0;
    state.pending.chunks = [packet.payload.slice(0, state.pending.receivedLength)];
  } else {
    if (packet.cid !== state.pending.cid || packet.sequence !== state.pending.nextSeq) {
      rejectPending(new Error("收到的续包顺序不正确。"));
      return;
    }
    chunk = packet.payload.slice(0, Math.min(CONT_PAYLOAD_SIZE, state.pending.totalLength - state.pending.receivedLength));
    state.pending.nextSeq += 1;
    state.pending.receivedLength += chunk.length;
    state.pending.chunks.push(chunk);
  }

  if (state.pending.totalLength !== null && state.pending.receivedLength >= state.pending.totalLength) {
    const payload = new Uint8Array(state.pending.totalLength);
    let offset = 0;
    for (const part of state.pending.chunks) {
      payload.set(part, offset);
      offset += part.length;
    }
    resolvePending({
      cid: state.pending.cid,
      command: state.pending.command,
      payloadLength: state.pending.totalLength,
      payload,
      raw: packet.raw,
    });
  }
}

async function runInit() {
  const nonce = crypto.getRandomValues(new Uint8Array(8));
  const packet = await sendCommand(BROADCAST_CID, COMMANDS.init, nonce);
  const parsed = parseInitPayload(packet.payload);

  state.channelId = Number.parseInt(parsed.channelId, 16) >>> 0;
  state.capabilities = parsed.capabilities;
  setText(elements.initOutput, JSON.stringify(parsed, null, 2));
  updateUi();
}

async function runPing() {
  const payload = utf8Encode(elements.pingInput.value);
  const packet = await sendCommand(state.channelId, COMMANDS.ping, payload);
  const decoded = utf8Decode(packet.payload);
  setText(
    elements.pingOutput,
    JSON.stringify(
      {
        length: packet.payload.length,
        utf8: decoded,
        hex: bytesToHexCompact(packet.payload),
      },
      null,
      2
    )
  );
}

async function runGetInfo() {
  const packet = await sendCommand(state.channelId, COMMANDS.cbor, new Uint8Array([0x04]));
  const parsed = parseInfoPayload(packet.payload);
  setText(elements.infoOutput, JSON.stringify(parsed, null, 2));
}

async function runMakeCredential() {
  const rpId = elements.rpIdInput.value.trim();
  const userId = utf8Encode(elements.userIdInput.value.trim());
  const userName = elements.userNameInput.value.trim();
  const displayName = elements.displayNameInput.value.trim();
  const excludeList = parseExcludeList(elements.excludeListInput.value);
  const clientDataHash = crypto.getRandomValues(new Uint8Array(32));
  const requestMap = new Map([
    [1, clientDataHash],
    [2, new Map([["id", rpId], ["name", rpId]])],
    [
      3,
      new Map([
        ["id", userId],
        ["name", userName],
        ["displayName", displayName],
      ]),
    ],
    [4, [{ type: "public-key", alg: -7 }]],
    [7, new Map([["rk", true], ["uv", false]])],
  ]);

  if (excludeList.length > 0) {
    requestMap.set(5, excludeList);
  }

  if (!rpId) {
    throw new Error("RP ID 不能为空。");
  }
  if (!userId.length) {
    throw new Error("用户 ID 不能为空。");
  }

  const payload = concatBytes(Uint8Array.of(0x01), encodeCbor(requestMap));
  const packet = await sendCommand(state.channelId, COMMANDS.cbor, payload);
  const status = readStatusByte(packet.payload, "authenticatorMakeCredential");
  const decoded = status === 0x00 ? decodeCbor(packet.payload.slice(1)) : { value: null };
  const credentialId = status === 0x00 ? extractCredentialIdFromMakeCredential(decoded.value) : null;

  if (credentialId) {
    state.lastCredentialIdHex = bytesToHexCompact(credentialId);
    elements.credentialIdInput.value = state.lastCredentialIdHex;
    saveUiState();
  }

  setText(
    elements.makeCredentialOutput,
    JSON.stringify(
      {
        status,
        statusName: ctapStatusName(status),
        clientDataHash: bytesToHexCompact(clientDataHash),
        request: {
          rpId,
          userId: bytesToHexCompact(userId),
          userName,
          displayName,
          excludeList: excludeList.map((entry) => bytesToHexCompact(entry.id)),
        },
        credentialId: credentialId ? bytesToHexCompact(credentialId) : null,
        response: status === 0x00 ? normalize(decoded.value) : null,
      },
      null,
      2
    )
  );
}

async function runGetAssertion() {
  const rpId = elements.rpIdInput.value.trim();
  const clientDataHash = crypto.getRandomValues(new Uint8Array(32));
  const credentialId = hexToBytes(elements.credentialIdInput.value);
  const requestMap = new Map([
    [1, rpId],
    [2, clientDataHash],
    [5, new Map([["uv", false]])],
  ]);

  if (!rpId) {
    throw new Error("RP ID 不能为空。");
  }

  if (credentialId.length > 0) {
    requestMap.set(3, [{ type: "public-key", id: credentialId }]);
  }

  const payload = concatBytes(Uint8Array.of(0x02), encodeCbor(requestMap));
  const packet = await sendCommand(state.channelId, COMMANDS.cbor, payload);
  const status = readStatusByte(packet.payload, "authenticatorGetAssertion");
  const decoded = status === 0x00 ? decodeCbor(packet.payload.slice(1)) : { value: null };

  setText(
    elements.getAssertionOutput,
    JSON.stringify(
      {
        status,
        statusName: ctapStatusName(status),
        clientDataHash: bytesToHexCompact(clientDataHash),
        request: {
          rpId,
          credentialId: credentialId.length > 0 ? bytesToHexCompact(credentialId) : null,
        },
        response: status === 0x00 ? normalize(decoded.value) : null,
      },
      null,
      2
    )
  );
}

function attachHandlers() {
  elements.connectButton.addEventListener("click", () => wrapAction(requestDevice));
  elements.reconnectButton.addEventListener("click", () => wrapAction(reconnectKnown));
  elements.disconnectButton.addEventListener("click", () => wrapAction(disconnect));
  elements.initButton.addEventListener("click", () => wrapAction(runInit));
  elements.pingButton.addEventListener("click", () => wrapAction(runPing));
  elements.getInfoButton.addEventListener("click", () => wrapAction(runGetInfo));
  elements.makeCredentialButton.addEventListener("click", () => wrapAction(runMakeCredential));
  elements.getAssertionButton.addEventListener("click", () => wrapAction(runGetAssertion));
  elements.clearLogButton.addEventListener("click", () => {
    elements.logOutput.innerHTML = "";
  });
  [
    elements.rpIdInput,
    elements.userIdInput,
    elements.userNameInput,
    elements.displayNameInput,
    elements.credentialIdInput,
    elements.excludeListInput,
  ].forEach((element) => {
    element?.addEventListener("input", saveUiState);
  });

  navigator.hid.addEventListener("disconnect", (event) => {
    if (state.device && event.device === state.device) {
      void disconnect();
    }
  });
}

async function wrapAction(action) {
  try {
    await action();
  } catch (error) {
    logEntry("错误", error instanceof Error ? error.message : String(error));
  }
}

function assertBrowserSupport() {
  if (!("hid" in navigator)) {
    throw new Error("当前浏览器不支持 WebHID，请使用较新的 Chromium 内核浏览器。");
  }
}

function bootstrap() {
  try {
    assertBrowserSupport();
    attachHandlers();
    loadUiState();
    updateUi();
    logEntry(
      "就绪",
      "控制台已加载。先请求设备权限，再执行 CTAPHID 初始化，然后可以测试 Ping、读取信息和注册凭据。"
    );
  } catch (error) {
    setBadge("不支持");
    logEntry("错误", error instanceof Error ? error.message : String(error));
  }
}

bootstrap();
