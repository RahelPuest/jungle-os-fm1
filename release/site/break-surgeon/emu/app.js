// made by tools/build.py: the emulator's parts (fm1.toml emu) and the app's own part of the page
import { compose } from "./parts/parts.js";
import * as midi from "./parts/midi.js";
import * as data from "./parts/data.js";
const own = {};
export const { attach, message } = compose({ midi, data }, own, { id: "breaksurgeon", name: "BREAK SURGEON", backupExt: "bak" });
