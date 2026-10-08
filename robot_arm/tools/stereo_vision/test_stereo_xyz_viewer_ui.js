"use strict";

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");

const reviewPath = process.argv[2];
if (!reviewPath) throw new Error("Pass a generated review.json for the browser-logic smoke test");
const review = JSON.parse(fs.readFileSync(reviewPath, "utf8"));
const template = fs.readFileSync(path.join(__dirname, "stereo_xyz_viewer.html"), "utf8");
assert.equal((template.match(/id="video"/g) || []).length, 1);
assert.match(template, /grid-template-columns:690px 690px/);
assert.match(template, /object-fit:contain/);
assert.ok(!template.includes("@media"), "Fixed layout must not reflow at smaller windows");
const source = template.split("<script>")[1].split("</script>")[0];
const nodes = new Map();

function node(identity = "") {
    const canvasContext = new Proxy({}, { get: (target, key) => key in target ? target[key] : () => {} });
    return {
        identity, value: identity === "metric" ? "elbow_z" : identity === "offset" ? "0" : "wrist",
        checked: false, children: [], width: 720, height: 440,
        readyState: 0, currentTime: 0, duration: NaN,
        textContent: identity === "data" ? JSON.stringify(review) : "",
        append(child) { this.children.push(child); },
        replaceChildren() { this.children = []; },
        getContext() { return canvasContext; },
        setPointerCapture() {}, load() {}
    };
}

const document = {
    getElementById(identity) {
        if (!nodes.has(identity)) nodes.set(identity, node(identity));
        return nodes.get(identity);
    },
    createElement() { return node(); }
};
const approvedIndex = review.frames.findIndex(frame => frame.PG && frame.PG.accepted);
assert.ok(approvedIndex >= 0, "Fixture requires an approved frame");
const environment = vm.createContext({ document, console, location: { hash: `#${approvedIndex}` },
    performance: { now: () => 0 }, requestAnimationFrame() {}, URL: { createObjectURL: () => "blob:test", revokeObjectURL() {} } });
vm.runInContext(source, environment);
assert.equal(vm.runInContext("currentIndex", environment), approvedIndex);
assert.match(nodes.get("gate").textContent, /APPROVED/);
assert.equal(nodes.get("coordinates").children.length, 6);
assert.equal(nodes.get("angles").children.length, 5);
nodes.get("next").onclick();
assert.equal(vm.runInContext("currentIndex", environment), approvedIndex + 1);
nodes.get("previous").onclick();
assert.equal(vm.runInContext("currentIndex", environment), approvedIndex);

vm.runInContext(`data.recording = { samples: [
    {utc:10,seconds:0,paused:false,active:true},
    {utc:20,seconds:10,paused:false,active:true}
] };`, environment);
assert.equal(vm.runInContext("videoSeconds(15)", environment), 5);
assert.equal(vm.runInContext("videoSeconds(10)", environment), 0);
assert.equal(vm.runInContext("videoSeconds(20)", environment), 10);
assert.equal(vm.runInContext("videoSeconds(21)", environment), null);
assert.equal(vm.runInContext("videoSeconds(9)", environment), null);
vm.runInContext("data.recording.samples[1].paused=true", environment);
assert.equal(vm.runInContext("videoSeconds(15)", environment), null);
vm.runInContext(`data.recording = {samples: [
    {utc:frames[0].receipt_seconds,seconds:0,paused:false,active:true},
    {utc:frames[frames.length-1].receipt_seconds,seconds:frames[frames.length-1].receipt_seconds-frames[0].receipt_seconds,paused:false,active:true}
],rects:{},video_size:[]}; showFrame(0);`, environment);
const lastIndex = review.frames.length - 1;
nodes.get("video").currentTime = review.frames[lastIndex].receipt_seconds - review.frames[0].receipt_seconds;
nodes.get("video").ontimeupdate();
assert.equal(vm.runInContext("currentIndex", environment), lastIndex);
assert.equal(nodes.get("video").currentTime, review.frames[lastIndex].receipt_seconds - review.frames[0].receipt_seconds);
console.log("Browser logic OK: exact selected pair, tables, navigation, timing boundaries and pause exclusion");
