import re,sys,urllib.request,urllib.parse,http.cookiejar,html
B="https://cims.coastal.louisiana.gov/DataDownload/DataDownload.aspx?type=hydro_hourly"
cj=http.cookiejar.CookieJar()
op=urllib.request.build_opener(urllib.request.HTTPCookieProcessor(cj))
op.addheaders=[("User-Agent","Mozilla/5.0 (research probe)")]
def fields(h):
    d={}
    for m in re.finditer(r'<input[^>]*>',h):
        t=m.group(0)
        n=re.search(r'name="([^"]+)"',t); v=re.search(r'value="([^"]*)"',t); ty=re.search(r'type="(\w+)"',t)
        if n and ty and ty.group(1) in("hidden","text"): d[n.group(1)]=html.unescape(v.group(1)) if v else ""
    return d
def post(h,extra,out=None):
    d=fields(h); d.update(extra)
    r=op.open(urllib.request.Request(B,urllib.parse.urlencode(d).encode()),timeout=120)
    return r.read(),r
h=op.open(B,timeout=60).read().decode("utf8","replace")
h2,r=post(h,{"__EVENTTARGET":"ctl00$MainContent$RBL_ProjCrmsList$2","ctl00$MainContent$RBL_ProjCrmsList":"Filter by Station"})
h2=h2.decode("utf8","replace")
open("/tmp/crms_probe/step2.html","w").write(h2)
print(len(h2)); 
for m in re.finditer(r'<(input|select)[^>]*(Station|Download|Preview)[^>]*>',h2): print(m.group(0)[:250])
import sys,time
FROM,TO,OUT=sys.argv[1:4]
S="ctl00$MainContent$"
ST="CRMS0002-H01 (09/29/2007 to 04/28/2026 [162,864])"
h3,r=post(h2,{S+"TB_StationsList":ST,S+"btnStation":"","__EVENTTARGET":""}); h3=h3.decode("utf8","replace")
open("/tmp/crms_probe/step3.html","w").write(h3)
print("step3",len(h3))
for m in re.finditer(r'<input[^>]*(BTN_DownLoad|FromDate|ToDate)[^>]*>',h3): print(m.group(0)[:200])
for m in re.finditer(r'id="MainContent_LBL_(Error|Info)[^"]*"[^>]*>([^<]*)',h3): print(m.group(0)[:300])
t0=time.time()
h4,r=post(h3,{S+"TB_StationsList":ST,S+"TB_FromDate":FROM,S+"TB_ToDate":TO,S+"BTN_DownLoad":"Download"})
print(r.status,r.headers.get("content-type"),r.headers.get("content-disposition"),len(h4))
h5=h4.decode("utf8","replace")
h6,r=post(h5,{S+"TB_StationsList":ST,S+"TB_FromDate":FROM,S+"TB_ToDate":TO,S+"TB_Filename":"probe1",S+"HF_Filename":"probe1",S+"BTN_OkFilename":"Ok"})
print(r.status,r.headers.get("content-type"),r.headers.get("content-disposition"),len(h6))
open(OUT,"wb").write(h6)

print("secs",time.time()-t0)
