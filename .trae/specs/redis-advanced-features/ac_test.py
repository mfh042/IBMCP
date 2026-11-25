import json, base64, urllib.request, urllib.error, subprocess, time, sys

B = "http://127.0.0.1:8000"
RCLI = ["sudo", "docker", "exec", "ibmcp-redis", "redis-cli"]
def redis(*args):
    return subprocess.run(RCLI + [str(a) for a in args], capture_output=True, text=True).stdout.strip()
def http(method, path, body=None, token=None):
    data=None; h={}
    if body is not None: data=json.dumps(body).encode(); h["Content-Type"]="application/json"
    if token: h["Authorization"]="Bearer "+token
    req=urllib.request.Request(B+path,data=data,method=method,headers=h)
    try:
        with urllib.request.urlopen(req,timeout=10) as r: return r.status,r.read().decode()
    except urllib.error.HTTPError as e: return e.code,e.read().decode()
def b64d(s): return base64.urlsafe_b64decode(s+"="*((4-len(s)%4)%4)).decode()
def body_code(b):
    try: return json.loads(b).get("code")
    except Exception: return None
res=[]
def check(name,cond,detail=""):
    res.append((name,bool(cond)))
    print(("PASS" if cond else "FAIL")+": "+name+((" | "+detail) if detail else ""))

st,body=http("POST","/api/v1/auth/login",{"username":"admin","password":"admin123"})
token1=json.loads(body)["data"]["token"]
jti1=json.loads(b64d(token1.split(".")[1])).get("jti","")
check("T1 login 200 + token", st==200 and bool(token1), "code="+str(st))
check("T1 jti present 32-hex", len(jti1)==32 and all(c in "0123456789abcdef" for c in jti1), "jti="+jti1)
st2,body2=http("POST","/api/v1/auth/login",{"username":"admin","password":"admin123"})
jti2=json.loads(b64d(json.loads(body2)["data"]["token"].split(".")[1])).get("jti","")
check("T1 jti unique across logins", jti1!=jti2 and jti2!="")

st,vb=http("GET","/api/v1/auth/verify",token=token1)
check("T3 token1 valid before logout", st==200 and body_code(vb)==0, "http="+str(st)+" body_code="+str(body_code(vb)))

redis("DEL","cache:static_dict:bootstrap")
st,b_a=http("GET","/api/v1/static/bootstrap",token=token1)
check("C1 bootstrap GET 200", st==200, "code="+str(st)+" body[:80]="+b_a[:80])
check("C1 bootstrap key populated", redis("EXISTS","cache:static_dict:bootstrap")=="1")
st,b_b=http("GET","/api/v1/static/bootstrap",token=token1)
check("C1 cached response byte-identical", b_a==b_b, "len="+str(len(b_a)))
ttl=int(redis("TTL","cache:static_dict:bootstrap"))
check("C1 bootstrap TTL in (0,600]", 0<ttl<=600, "ttl="+str(ttl))

http("GET","/api/v1/data/history?device_code=x&start=2020-01-01&end=2020-01-02&interval=60",token=token1)
check("C2 history not cached", redis("KEYS","cache:data:history*")=="")
http("GET","/api/v1/data/latest?device_code=x",token=token1)
check("C2 latest cached", redis("KEYS","cache:data:latest*")!="")

bdata=json.loads(b_a).get("data",{})
target_code=None; target_id=None
for code,items in bdata.items():
    if isinstance(items,list) and items:
        for it in items:
            if isinstance(it,dict) and "id" in it:
                target_code=code; target_id=it["id"]; break
    if target_code: break
check("C3 found dict item to update", target_code is not None, "code="+str(target_code)+" id="+str(target_id))
if target_code:
    allkey="cache:static_dict:"+target_code+":all"
    redis("DEL",allkey,"cache:static_dict:bootstrap")
    http("GET","/api/v1/static/"+target_code+"/all",token=token1)
    http("GET","/api/v1/static/bootstrap",token=token1)
    pre=redis("EXISTS",allkey)=="1" and redis("EXISTS","cache:static_dict:bootstrap")=="1"
    check("C3 pre-write keys exist", pre)
    st,ibody=http("GET","/api/v1/static/"+target_code+"/"+str(target_id),token=token1)
    item=json.loads(ibody).get("data",{})
    for _ro in ("id","created_at","updated_at"): item.pop(_ro,None)
    st,pbody=http("PUT","/api/v1/static/"+target_code+"/"+str(target_id),body=item,token=token1)
    check("C3 update success (body code 0)", st==200 and body_code(pbody)==0, "http="+str(st)+" body="+pbody[:120])
    check("C3 bootstrap invalidated", redis("EXISTS","cache:static_dict:bootstrap")=="0")
    check("C3 all invalidated", redis("EXISTS",allkey)=="0")

st,lbody=http("POST","/api/v1/auth/logout",body={},token=token1)
check("T2 logout 200 + logged_out", st==200 and json.loads(lbody).get("data",{}).get("logged_out")==True, "code="+str(st))
blkey="auth:blacklist:"+jti1
check("T2 blacklist key exists", redis("EXISTS",blkey)=="1","key="+blkey)
bttl=int(redis("TTL",blkey)); check("T2 blacklist TTL>0", bttl>0, "ttl="+str(bttl))
st,vbody=http("GET","/api/v1/auth/verify",token=token1)
check("T3 revoked token rejected (body code 401)", body_code(vbody)==401, "http="+str(st)+" body="+vbody[:120])
check("T3 message token revoked", "token revoked" in vbody)

st,body=http("POST","/api/v1/auth/login",{"username":"admin","password":"admin123"})
tf=json.loads(body)["data"]["token"]
redis("DEL","cache:config:devices:route:0")
http("GET","/api/v1/config/devices?route_id=0",token=tf)
check("C1b config devices list cached", redis("EXISTS","cache:config:devices:route:0")=="1")
http("GET","/api/v1/config/devices?route_id=999999",token=tf)
check("C1b route variants keyed separately", redis("EXISTS","cache:config:devices:route:999999")=="1")
redis("DEL","cache:runtime:devices:overview")
http("GET","/api/v1/runtime/devices/overview",token=tf)
check("C1c runtime overview cached", redis("EXISTS","cache:runtime:devices:overview")=="1")

http("GET","/api/v1/config/routes?config_id=0",token=tf)
check("C1d config routes list cached", redis("EXISTS","cache:config:routes:cfg:0")=="1")

# C2e: device 写后失效 route expand 缓存（review Finding 1 修复，防回归）
_,rb=http("GET","/api/v1/config/routes",token=tf)
ritems=json.loads(rb).get("data",{}).get("items",[])
if ritems:
    rid=ritems[0]["id"]; ekey="cache:config:routes:%d:expand"%rid
    http("GET","/api/v1/config/routes/%d/expand"%rid,token=tf)
    check("C2e expand cached before device write", redis("EXISTS",ekey)=="1", "key="+ekey)
    _,db=http("GET","/api/v1/config/devices?route_id=%d"%rid,token=tf)
    ditems=json.loads(db).get("data",{}).get("items",[])
    if ditems:
        bid=ditems[0]["id"]; b=dict(ditems[0])
        for f in ("id","created_at","updated_at"): b.pop(f,None)
        st,ub=http("PUT","/api/v1/config/devices/%d"%bid,token=tf,body=b)
        check("C2e device update ok", body_code(ub)==0, "http="+str(st))
        check("C2e expand invalidated after device write", redis("EXISTS",ekey)=="0")
        http("GET","/api/v1/config/routes/%d/expand"%rid,token=tf)
        check("C2e expand repopulated after re-GET", redis("EXISTS",ekey)=="1")
    else:
        print("SKIP: C2e route has no device bindings")
else:
    print("SKIP: C2e no routes in env")

print("=== SUMMARY ===")
p=sum(1 for _,c in res if c); print("PASSED %d/%d"%(p,len(res)))
sys.exit(0 if p==len(res) else 1)
