#include "web_config.h"

#include <Arduino.h>
#include <WebServer.h>

#include "config.h"
#include "service_list.h"

#if __has_include("secrets.h")
#include "secrets.h"
#endif
#ifndef OTA_PASSWORD
#define OTA_PASSWORD ""
#endif

namespace {

constexpr size_t kMaxBodyBytes = 8 * 1024;

WebServer server(WEB_PORT);
ServiceMonitor* gMonitor = nullptr;
ServiceList gList;  // scratch; only touched from the loop task
char gText[MAX_SERVICES * 200 + 1];

const char kPage[] PROGMEM = R"HTML(<!DOCTYPE html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>service-monitor</title>
<style>
body{background:#000;color:#cfffdc;font:15px ui-monospace,Menlo,monospace;max-width:760px;margin:0 auto;padding:16px}
h1{color:#2bff6e;font-size:18px}textarea{width:100%;height:280px;background:#020a05;color:#cfffdc;border:1px solid #2bff6e;border-radius:8px;padding:10px;font:inherit;box-sizing:border-box}
button{background:#2bff6e;color:#000;border:0;border-radius:6px;padding:8px 14px;font:inherit;font-weight:bold;margin:8px 8px 0 0;cursor:pointer}
#msg{margin-top:12px;white-space:pre-wrap}
.err{color:#ff3b5c}.ok{color:#2bff6e}code{color:#4fe3ff}p{color:#3fa162}
</style></head><body>
<h1>&gt; service-monitor / services</h1>
<p>One service per line, up to 16. Lines starting with # are ignored.<br>
<b style="color:#ffb000">The screen has room for 5.</b> All are checked, but only 5 are shown at once and problems always move to the top, so extra ones appear as soon as they go down.<br>
<code>statuspage | Name | https://status.example.com</code> provider status page<br>
<code>http | Name | https://host/path</code> any 2xx/3xx is healthy<br>
<code>http | Name | https://host/health | 200</code> require an exact code</p>
<textarea id="list" spellcheck="false"></textarea><br>
<button onclick="save()">Save to board</button>
<div id="msg"></div>
<script>
const $=id=>document.getElementById(id);
function show(t,ok){$('msg').textContent=t;$('msg').className=ok?'ok':'err'}
async function load(){const r=await fetch('/services');$('list').value=await r.text();show('',true)}
async function save(){const r=await fetch('/services',{method:'PUT',headers:{'Content-Type':'text/plain'},body:$('list').value});show(await r.text(),r.ok);if(r.ok)load()}
load();
</script></body></html>)HTML";

bool authorized() {
    if (strlen(OTA_PASSWORD) == 0) return true;
    if (server.authenticate("admin", OTA_PASSWORD)) return true;
    server.requestAuthentication();
    return false;
}

void handlePage() {
    if (!authorized()) return;
    server.send_P(200, "text/html", kPage);
}

void handleGet() {
    if (!authorized()) return;
    gMonitor->getServices(gList);
    formatServiceText(gList, gText, sizeof(gText));
    server.send(200, "text/plain; charset=utf-8", gText);
}

void handlePut() {
    if (!authorized()) return;
    if (!server.hasArg("plain")) {
        server.send(400, "text/plain", "error: send the list as the request body\n");
        return;
    }
    const String& body = server.arg("plain");
    if (body.length() > kMaxBodyBytes) {
        server.send(413, "text/plain", "error: list too large\n");
        return;
    }

    char err[80];
    if (!parseServiceText(body.c_str(), gList, err, sizeof(err))) {
        server.send(400, "text/plain", String("error: ") + err + "\n");
        return;
    }
    if (!saveServices(gList)) {
        server.send(500, "text/plain", "error: could not save to flash\n");
        return;
    }
    gMonitor->setServices(gList);
    server.send(200, "text/plain", String("saved ") + gList.count + " services\n");
}

void handleReset() {
    if (!authorized()) return;
    eraseSavedServices();
    loadDefaultServices(gList);
    gMonitor->setServices(gList);
    server.send(200, "text/plain", String("restored ") + gList.count + " default services\n");
}

}  // namespace

void WebConfig::begin() {
    if (started_) return;
    gMonitor = &monitor_;

    server.on("/", HTTP_GET, handlePage);
    server.on("/services", HTTP_GET, handleGet);
    server.on("/services", HTTP_PUT, handlePut);
    server.on("/services", HTTP_POST, handlePut);
    server.on("/services/reset", HTTP_POST, handleReset);
    server.onNotFound([]() { server.send(404, "text/plain", "not found\n"); });
    server.begin();
    started_ = true;
    Serial.printf("[web] service editor at http://%s.local/\n", OTA_HOSTNAME);
}

void WebConfig::loop() {
    if (started_) server.handleClient();
}
