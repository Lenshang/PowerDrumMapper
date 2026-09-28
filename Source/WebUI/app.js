/* ============================================================================
   PowerDrumMapper — WebView UI logic.

   Talks to the plugin over the JUCE native integration bridge:
     UI -> plugin:  window.__JUCE__.backend.emitEvent("uiCommand", command)
     plugin -> UI:  "stateChanged" / "toast" events.

   The note-name helpers mirror Source/NoteNameUtils.cpp exactly (Bitwig/DAW
   convention: MIDI 60 -> "C3").
   ============================================================================ */

"use strict";

/* ---------------------------------------------------------------- helpers -- */

const NOTE_NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];

function midiToName(n) {
  if (!(n >= 0 && n < 128)) return "";
  return NOTE_NAMES[n % 12] + (Math.floor(n / 12) - 2);
}

function midiToNameWithNumber(n) {
  const name = midiToName(n);
  return name === "" ? "---" : name + " (" + n + ")";
}

function letterToPitchClass(c) {
  const map = { C: 0, D: 2, E: 4, F: 5, G: 7, A: 9, B: 11 };
  return map[c.toUpperCase()] ?? -1;
}

/* Mirror of NoteNameUtils::nameToMidi ("C1", "Db0", "D#2", "c-1", ...). */
function nameToMidi(input) {
  const t = input.trim();
  if (t === "") return -1;

  const m = /^([A-Ga-g])([#b]*)\s*([-+]?\d+)$/.exec(t);
  if (!m) return -1;

  let pc = letterToPitchClass(m[1]);
  if (pc < 0) return -1;

  for (const ch of m[2]) pc += ch === "#" ? 1 : -1;
  pc = ((pc % 12) + 12) % 12;

  const octave = parseInt(m[3], 10);
  const note = (octave + 2) * 12 + pc;

  return note >= 0 && note < 128 ? note : -1;
}

/* Mirror of NoteNameUtils::parseNoteText: "C1 (36)", "C1", "db0", "36". */
function parseNoteText(input) {
  const t = input.trim();
  if (t === "") return -1;

  if (t.includes("(") || t.includes(")")) {
    if (!(t.includes("(") && t.endsWith(")"))) return -1;
    const inner = t.slice(t.indexOf("(") + 1, t.lastIndexOf(")")).trim();
    if (!/^\d+$/.test(inner)) return -1;
    const v = parseInt(inner, 10);
    return v >= 0 && v < 128 ? v : -1;
  }

  const byName = nameToMidi(t);
  if (byName >= 0) return byName;

  if (/^\d+$/.test(t)) {
    const v = parseInt(t, 10);
    return v >= 0 && v < 128 ? v : -1;
  }

  return -1;
}

function clampNote(n) { return Math.min(127, Math.max(0, n)); }

function clampChannel(c) { return Math.min(16, Math.max(0, c)); }

function channelLabel(c) { return c === 0 ? "ALL" : String(c); }

/* Mirror of NoteMapping::fromCsvString ("name,srcNote,srcCh,tgtNote,tgtCh"):
   empty lines are skipped, lines with fewer than 5 fields are skipped, and
   numeric fields are CLAMPED (notes 0-127, channels 0-16) — never rejected —
   so the preview parses exactly like the plugin. */
function parseBwdrmCsv(text) {
  const rows = [];
  for (const rawLine of String(text).split(/\r?\n/)) {
    const line = rawLine.trim();
    if (line === "") continue;

    const tokens = line.split(",").map((t) => t.trim());
    if (tokens.length < 5) continue;

    const clamp = (v, lo, hi) => Math.min(hi, Math.max(lo, Math.floor(Number(v)) || 0));
    rows.push({
      name: tokens[0],
      sourceNote: clamp(tokens[1], 0, 127),
      sourceChannel: clamp(tokens[2], 0, 16),
      targetNote: clamp(tokens[3], 0, 127),
      targetChannel: clamp(tokens[4], 0, 16),
    });
  }
  return rows;
}

function el(tag, className, text) {
  const e = document.createElement(tag);
  if (className) e.className = className;
  if (text !== undefined) e.textContent = text;
  return e;
}

/* ----------------------------------------------------------------- bridge -- */

const backend = window.__JUCE__ ? window.__JUCE__.backend : null;

function send(command) {
  if (backend) backend.emitEvent("uiCommand", command);
}

if (backend) {
  backend.addEventListener("stateChanged", (s) => applyState(s));
  backend.addEventListener("toast", (t) => showToast(t.kind || "info", t.text || ""));
}

/* Mock backend so the UI can be designed in a plain browser: open with ?mock */
if (!backend && window.location.search.includes("mock")) {
  const mockState = {
    entries: [
      { name: "Kick",   sourceNote: 24, sourceChannel: 0, targetNote: 36, targetChannel: 10 },
      { name: "Snare",  sourceNote: 26, sourceChannel: 0, targetNote: 38, targetChannel: 10 },
      { name: "ESnare", sourceNote: 27, sourceChannel: 0, targetNote: 39, targetChannel: 10 },
      { name: "HiHat",  sourceNote: 42, sourceChannel: 0, targetNote: 44, targetChannel: 10 },
    ],
  };
  window.__JUCE__ = {
    backend: {
      emitEvent(id, payload) {
        if (id !== "uiCommand") return;
        switch (payload.type) {
          case "requestState": break;
          case "setEntryName": mockState.entries[payload.index].name = payload.name; break;
          case "setEntrySourceNote": mockState.entries[payload.index].sourceNote = payload.note; break;
          case "setEntrySourceChannel": mockState.entries[payload.index].sourceChannel = payload.channel; break;
          case "setEntryTargetNote": mockState.entries[payload.index].targetNote = payload.note; break;
          case "setEntryTargetChannel": mockState.entries[payload.index].targetChannel = payload.channel; break;
          case "addEntry":
            mockState.entries.splice(payload.index ?? mockState.entries.length, 0,
                                     { name: "New", sourceNote: 60, sourceChannel: 0, targetNote: 60, targetChannel: 0 });
            break;
          case "removeEntry": mockState.entries.splice(payload.index, 1); break;
          case "clearEntries": mockState.entries = []; break;
          default: console.log("mock uiCommand", payload);
        }
        applyState(mockState);
      },
      addEventListener() {},
    },
  };
  window.__JUCE__.backend.emitEvent("uiCommand", { type: "requestState" });
}

/* -------------------------------------------------------------- app state -- */

let state = { entries: [] };

const tableBody = document.getElementById("tableBody");
const emptyState = document.getElementById("emptyState");
const statRulesEl = document.getElementById("statRules");
const entryCountEl = document.getElementById("entryCount");

/* ------------------------------------------------------------ table view -- */

/* Keep DOM rows when only values change, so in-progress edits (and focus)
   survive host-driven state updates. */
function renderTable() {
  const count = state.entries.length;
  const previousDomCount = tableBody.children.length;
  emptyState.hidden = count > 0;

  /* Rules sharing the same (source note, source channel) match condition:
     only the first one can ever fire. */
  const dupKeys = new Set();
  {
    const seen = new Set();
    for (const e of state.entries) {
      const key = e.sourceNote + "/" + e.sourceChannel;
      if (seen.has(key)) dupKeys.add(key);
      seen.add(key);
    }
  }

  while (tableBody.children.length > count) tableBody.lastChild.remove();
  while (tableBody.children.length < count) tableBody.appendChild(buildRow(tableBody.children.length));

  for (let i = 0; i < count; ++i) {
    const row = tableBody.children[i];
    row.dataset.index = i;
    const entry = state.entries[i];

    const nameInput = row.querySelector(".name-input");
    if (nameInput.value !== entry.name && document.activeElement !== nameInput)
      nameInput.value = entry.name;

    const srcNoteInput = row.querySelector(".src-note-input");
    const srcLabel = midiToNameWithNumber(entry.sourceNote);
    if (srcNoteInput.value !== srcLabel && document.activeElement !== srcNoteInput)
      srcNoteInput.value = srcLabel;

    const srcChannel = row.querySelector(".src-channel");
    if (srcChannel.value !== String(entry.sourceChannel)) srcChannel.value = String(entry.sourceChannel);

    const tgtNoteInput = row.querySelector(".tgt-note-input");
    const tgtLabel = midiToNameWithNumber(entry.targetNote);
    if (tgtNoteInput.value !== tgtLabel && document.activeElement !== tgtNoteInput)
      tgtNoteInput.value = tgtLabel;

    const tgtChannel = row.querySelector(".tgt-channel");
    if (tgtChannel.value !== String(entry.targetChannel)) tgtChannel.value = String(entry.targetChannel);

    const dup = dupKeys.has(entry.sourceNote + "/" + entry.sourceChannel);
    row.classList.toggle("dup", dup);
    row.title = dup
      ? "Another rule already matches " + midiToName(entry.sourceNote)
        + (entry.sourceChannel === 0 ? " on all channels" : " on channel " + entry.sourceChannel)
        + " \u2014 only the first one fires."
      : "";
  }

  statRulesEl.textContent = count;
  entryCountEl.textContent = count === 0 ? "" : count + (count === 1 ? " rule" : " rules");

  if (focusNewRowPending && count > previousDomCount) {
    focusNewRowPending = false;
    const last = tableBody.lastChild;
    const nameInput = last.querySelector(".name-input");
    nameInput.focus();
    nameInput.select();
    last.scrollIntoView({ block: "nearest" });
  } else if (count <= previousDomCount) {
    focusNewRowPending = false;
  }
}

function buildRow(index) {
  const row = el("div", "row");
  const entry = state.entries[index];

  const nameInput = el("input", "cell name-input");
  nameInput.type = "text";
  nameInput.spellcheck = false;
  nameInput.placeholder = "Name";
  nameInput.value = entry.name;
  wireNameInput(nameInput);
  row.appendChild(nameInput);

  row.appendChild(buildNoteCell(entry.sourceNote, "src-note-input"));
  row.appendChild(buildChannelCell(entry.sourceChannel, "src-channel", "Source channel"));
  row.appendChild(buildNoteCell(entry.targetNote, "tgt-note-input"));
  row.appendChild(buildChannelCell(entry.targetChannel, "tgt-channel", "Target channel (ALL keeps the original)"));

  const del = el("button", "row-del");
  del.type = "button";
  del.title = "Remove rule";
  del.innerHTML =
    '<svg width="13" height="13" viewBox="0 0 24 24" fill="none" stroke="currentColor" ' +
    'stroke-width="2.2" stroke-linecap="round"><path d="M18 6 6 18M6 6l12 12"/></svg>';
  del.addEventListener("click", () => {
    send({ type: "removeEntry", index: Number(row.dataset.index) });
  });
  row.appendChild(del);

  return row;
}

function buildNoteCell(note, inputClass) {
  const wrap = el("div", "note-wrap");
  const input = el("input", "cell note-input " + inputClass);
  input.type = "text";
  input.spellcheck = false;
  input.placeholder = "C3 (60)";
  input.value = midiToNameWithNumber(note);
  wireNoteInput(input);
  wrap.appendChild(input);
  wrap.appendChild(buildSuggestBox());
  return wrap;
}

function buildChannelCell(channel, selectClass, title) {
  const wrap = el("div", "select-wrap");
  const select = el("select", "cell " + selectClass);
  select.title = title;

  const allOption = el("option", null, "ALL");
  allOption.value = "0";
  select.appendChild(allOption);
  for (let c = 1; c <= 16; ++c) {
    const opt = el("option", null, String(c));
    opt.value = String(c);
    select.appendChild(opt);
  }
  select.value = String(channel);

  select.addEventListener("change", () => {
    const index = Number(select.closest(".row").dataset.index);
    send({
      type: selectClass === "src-channel" ? "setEntrySourceChannel" : "setEntryTargetChannel",
      index,
      channel: clampChannel(parseInt(select.value, 10)),
    });
  });

  wrap.appendChild(select);
  return wrap;
}

/* name editing: commit on Enter / blur; revert on Escape. */
function wireNameInput(input) {
  const row = () => input.closest(".row");

  const commit = () => {
    const value = input.value;
    if (value !== state.entries[Number(row().dataset.index)].name)
      send({ type: "setEntryName", index: Number(row().dataset.index), name: value });
  };

  input.addEventListener("keydown", (e) => {
    if (e.key === "Enter") {
      e.preventDefault();
      commit();
      const noteInput = row().querySelector(".src-note-input");
      noteInput.focus();
      noteInput.select();
    } else if (e.key === "Escape") {
      input.value = state.entries[Number(row().dataset.index)].name;
      input.blur();
    }
  });

  input.addEventListener("blur", commit);
}

/* note editing: autocomplete dropdown + parse-or-revert commit. */
function wireNoteInput(input) {
  const row = () => input.closest(".row");
  const isSource = input.classList.contains("src-note-input");

  const currentStateNote = () => {
    const entry = state.entries[Number(row().dataset.index)];
    return isSource ? entry.sourceNote : entry.targetNote;
  };

  const commit = (note) => {
    note = clampNote(note);
    const index = Number(row().dataset.index);
    const current = currentStateNote();
    if (current !== note) {
      send({ type: isSource ? "setEntrySourceNote" : "setEntryTargetNote", index, note });
    }
    input.value = midiToNameWithNumber(note);
  };

  const commitFromText = () => {
    const parsed = parseNoteText(input.value);
    commit(parsed >= 0 ? parsed : currentStateNote());
  };

  input.addEventListener("keydown", (e) => {
    const box = row().querySelector(".suggest");
    const suggestOpen = !!box && !box.hidden;

    if (e.key === "ArrowDown" || e.key === "ArrowUp") {
      if (suggestOpen) {
        e.preventDefault();
        moveSuggestActive(box, e.key === "ArrowDown" ? 1 : -1, input);
      }
      return;
    }

    if (e.key === "Enter") {
      e.preventDefault();
      const active = suggestOpen ? getSuggestActive(box) : null;
      if (active !== null) {
        pickSuggestion(box, active, input);
      } else {
        commitFromText();
        hideSuggestions(row());
        input.blur();
      }
      return;
    }

    if (e.key === "Escape") {
      input.value = midiToNameWithNumber(currentStateNote());
      hideSuggestions(row());
      input.blur();
    }
  });

  input.addEventListener("input", () => showSuggestions(row()));

  input.addEventListener("blur", () => {
    commitFromText();
    hideSuggestions(row());
  });

  input.addEventListener("focus", () => showSuggestions(row()));
}

/* suggestions */

function buildSuggestBox() {
  const box = el("div", "suggest");
  box.hidden = true;
  box.addEventListener("mousedown", (e) => e.preventDefault()); // keep input focus
  return box;
}

function matchedNotes(query) {
  const text = query.trim().toLowerCase();
  const all = [];
  for (let n = 0; n < 128; ++n) all.push(n);
  if (text === "") return all;
  const starts = [];
  const contains = [];
  for (const n of all) {
    const label = midiToNameWithNumber(n).toLowerCase();
    if (label.startsWith(text)) starts.push(n);
    else if (label.includes(text)) contains.push(n);
  }
  return starts.concat(contains);
}

function showSuggestions(row) {
  const input = row.querySelector(":scope > .note-wrap > .note-input");
  if (input !== document.activeElement) return;

  const box = input.parentElement.querySelector(".suggest");
  const matches = matchedNotes(input.value);

  box.textContent = "";
  box.dataset.active = "0";

  if (matches.length === 0) {
    box.appendChild(el("div", "suggest-item no-match", "(no match)"));
  } else {
    matches.forEach((n, i) => {
      const item = el("div", "suggest-item" + (i === 0 ? " active" : ""));
      item.dataset.note = n;
      item.appendChild(el("b", null, midiToName(n)));
      item.appendChild(el("i", null, String(n)));
      item.addEventListener("mousedown", (e) => {
        e.preventDefault();
        pickSuggestion(box, i, input);
      });
      box.appendChild(item);
    });
  }

  box.hidden = false;
  positionSuggestBox(input, box);
}

function positionSuggestBox(input, box) {
  const list = input.closest(".table-body");
  const inputRect = input.getBoundingClientRect();
  const listRect = list.getBoundingClientRect();
  const boxH = box.offsetHeight;

  box.style.top = "";
  if (inputRect.bottom + 4 + boxH > listRect.bottom && inputRect.top - 4 - boxH >= listRect.top)
    box.style.top = "calc(100% + 4px)";
  else if (inputRect.bottom + 4 + boxH > listRect.bottom)
    box.style.bottom = "0";
}

function hideSuggestions(row) {
  row.querySelectorAll(".suggest").forEach((box) => { box.hidden = true; });
}

function getSuggestActive(box) {
  if (!box || box.hidden || box.querySelector(".no-match")) return null;
  return Math.min(Number(box.dataset.active || 0), box.children.length - 1);
}

function moveSuggestActive(box, delta, input) {
  if (!box || box.hidden) return;
  let i = Number(box.dataset.active || 0) + delta;
  i = Math.max(0, Math.min(box.children.length - 1, i));
  box.dataset.active = i;
  [...box.children].forEach((c, ci) => c.classList.toggle("active", ci === i));
  box.children[i].scrollIntoView({ block: "nearest" });
  if (box.children[i].dataset.note !== undefined)
    input.value = midiToNameWithNumber(Number(box.children[i].dataset.note));
}

function pickSuggestion(box, index, input) {
  const item = box.children[index];
  if (!item || item.dataset.note === undefined) return;
  const note = Number(item.dataset.note);
  const rowEl = input.closest(".row");
  const isSource = input.classList.contains("src-note-input");
  const rowIndex = Number(rowEl.dataset.index);
  const current = isSource
    ? state.entries[rowIndex].sourceNote
    : state.entries[rowIndex].targetNote;

  if (current !== note)
    send({ type: isSource ? "setEntrySourceNote" : "setEntryTargetNote", index: rowIndex, note });

  input.value = midiToNameWithNumber(note);
  hideSuggestions(rowEl);
  input.blur();
}

/* ---------------------------------------------------------------- toolbar -- */

/* Set when the user asks for a new row; consumed by renderTable once the
   stateChanged echo has actually appended it (focusing earlier would race the
   async rebuild and land the caret in the wrong row). */
let focusNewRowPending = false;

function addEntry() {
  focusNewRowPending = true;
  send({ type: "addEntry", index: state.entries.length });
}

document.getElementById("addBtn").addEventListener("click", addEntry);
document.getElementById("emptyAddBtn").addEventListener("click", addEntry);
document.getElementById("clearBtn").addEventListener("click", () => send({ type: "clearEntries" }));
document.getElementById("emptyImportBtn").addEventListener("click", () => send({ type: "importFile" }));
document.getElementById("importBtn").addEventListener("click", () => send({ type: "importFile" }));
document.getElementById("exportBtn").addEventListener("click", () => send({ type: "exportFile" }));

/* ----------------------------------------------------------------- toasts -- */

function showToast(kind, text) {
  const host = document.getElementById("toastHost");
  const toast = el("div", "toast" + (kind === "error" ? " error" : ""), text);
  host.appendChild(toast);
  setTimeout(() => {
    toast.classList.add("leaving");
    setTimeout(() => toast.remove(), 300);
  }, kind === "error" ? 6000 : 2600);
}

/* -------------------------------------------------------------- bootstrap -- */

function applyState(s) {
  state = s;
  renderTable();
}

if (!backend && !window.location.search.includes("mock")) {
  showToast("error", "Native bridge unavailable \u2014 the UI cannot reach the plugin backend.");
}

renderTable();
send({ type: "requestState" });
