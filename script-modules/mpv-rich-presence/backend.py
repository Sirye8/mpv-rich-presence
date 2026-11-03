# SPDX-License-Identifier: AGPL-3.0-or-later
"""Optional off-thread account and metadata worker, one per interested player."""
import contextlib
import hashlib
import json
import os
from pathlib import Path
import re
import secrets
import sqlite3
import sys
import time

# mpv starts Python in isolated mode. Load siblings only from this installation.
sys.path.insert(0, str(Path(__file__).resolve().parent))
from account import AccountError, Vault, authorize, graphql, read_entry, save_progress
from providers import candidates, details, fingerprint


@contextlib.contextmanager
def serialized(directory):
    path = directory / "anilist-write.lock"
    with open(path, "a+b") as f:
        if os.name == "nt":
            import msvcrt
            if f.tell() == 0:
                f.write(b"0")
                f.flush()
            f.seek(0)
            msvcrt.locking(f.fileno(), msvcrt.LK_LOCK, 1)
        else:
            import fcntl
            fcntl.flock(f, fcntl.LOCK_EX)
        yield


class Worker:
    def __init__(self, db_path, window, settings):
        self.db = sqlite3.connect(db_path, isolation_level=None, timeout=4)
        self.directory = Path(db_path).parent
        self.window = window
        self.settings = settings
        self.vault = Vault()
        self.previous = None
        self.seen_account = None

    def get(self, category, scope, default=None):
        row = self.db.execute("SELECT value FROM records WHERE category=? AND scope=?", (category,scope)).fetchone()
        return json.loads(row[0]) if row else ({} if default is None else default)

    def put(self, category, scope, value):
        self.db.execute("INSERT OR REPLACE INTO records VALUES(?,?,?)", (category,scope,json.dumps(value)))

    def delete(self, category, scope):
        self.db.execute("DELETE FROM records WHERE category=? AND scope=?", (category,scope))

    @contextlib.contextmanager
    def transaction(self):
        self.db.execute("BEGIN IMMEDIATE")
        try:
            yield
        except BaseException:
            self.db.rollback()
            raise
        else:
            self.db.commit()

    def reply(self, **fields):
        with self.transaction():
            response = self.get("response",self.window)
            response.update(fields)
            self.put("response",self.window,response)

    def context(self, job, media=False):
        account = self.get("account","current")
        if job.get("account") != account.get("generation"):
            raise AccountError("Account changed. Review the action again.")
        player = self.get("player",self.window)
        if (job.get("session") != player.get("session",0) or
            (media and (job.get("media") != player.get("media") or not player.get("allowed")))):
            raise AccountError("Playback or privacy settings changed.")
        return account,player

    def credential(self, account):
        token = self.vault.read(account.get("generation",""))
        if not token:
            raise AccountError("Reconnect to AniList in this window.")
        return token

    def cycle(self, account, player):
        return "%s/%s/%s" % (account["id"],player["media"],player["episode"])

    def write(self, job):
        with serialized(self.directory):
            account,p = self.context(job, True)
            token = self.credential(account)
            base = job["base"]
            if base.get("viewer") != account["id"] or base.get("session") != p["session"] or base.get("media") != p["media"]:
                raise AccountError("The draft no longer belongs to this entry.")
            if job.get("automatic") and (not p.get("watch") or p["tracking"].get("mode")!="auto" or not p.get("safe_episode")):
                raise AccountError("Automatic tracking is off for this file.")
            def validate():
                current, player=self.context(job,True)
                if job.get("automatic") and (player["tracking"].get("mode")!="auto" or not player["active"] or
                    not self.get("pending",self.cycle(current,player))):
                    raise AccountError("Automatic proposal expired.")
            result = save_progress(token,base,job["target"],p["total"],validate)
            self.context(job,True)
            key=self.cycle(account,p)
            with self.transaction():
                self.put("history",key,{"progress":result["progress"],"time":time.time(),"window":self.window,"session":p["session"]})
                self.delete("pending",key)
            self.reply(entry=dict(result,session=p["session"]),proposal=None,message="Progress saved.")

    def throttle(self, provider):
        with self.transaction():
            now=time.time()
            when=max(now, self.get("rate",provider).get("next",0))
            self.put("rate",provider,{"next":when + (1.1 if provider=="musicbrainz" else 0.5)})
        time.sleep(max(0,when-now))

    def handle(self, job):
        op = job["op"]
        account,p = self.context(job,op in ("read","write","search","select","accept","skip"))
        if op == "connect":
            token=authorize(self.settings.get("client_id"),lambda:self.context(job), vault=self.vault)
            viewer=graphql("query{Viewer{id name avatar{medium}}}",{},token)["Viewer"]
            with serialized(self.directory):
                self.context(job)
                generation=secrets.token_hex(16)
                persistent=self.vault.write(generation,token)
                if account.get("generation"):
                    self.vault.forget(account["generation"],account.get("persistent"))
                self.put("account","current",dict(id=viewer["id"],name=viewer["name"],avatar=viewer["avatar"],
                    generation=generation,persistent=persistent,window=self.window))
            self.reply(entry=None,proposal=None,message="Connected." if persistent else "Connected for this session.")
        elif op in ("disconnect","reset"):
            with serialized(self.directory):
                self.context(job)
                if account.get("generation"):
                    self.vault.forget(account["generation"],account.get("persistent"))
                with self.transaction():
                    self.delete("account","current")
                    self.db.execute("DELETE FROM records WHERE category IN ('job','pending','response')")
                    if op=="reset":
                        self.db.execute("DELETE FROM records WHERE category!='player'")
                        self.put("privacy","global",{"mode":"hidden","block_lookup":True})
            self.reply(message="Disconnected.")
        elif op == "read":
            entry=read_entry(self.credential(account),account["id"],p["media"])
            self.context(job,True)
            self.reply(entry=dict(entry,session=p["session"]),offline=False,message="Progress refreshed.")
        elif op == "write":
            self.write(job)
        elif op in ("accept","skip"):
            key=self.cycle(account,p)
            prop=self.get("pending",key)
            if not prop or prop["key"]!=job["proposal"] or prop["account"]!=account["generation"]:
                raise AccountError("This proposal has expired.")
            if op=="accept":
                self.write(dict(job,base=prop["base"],target=prop["episode"]))
            else:
                with self.transaction():
                    self.put("history",key,{"skip":True})
                    self.delete("pending",key)
                self.reply(proposal=None,message="Skipped.")
        elif op == "clear":
            category=job["category"]
            with serialized(self.directory), self.transaction():
                if category in ("appearance","privacy","tracking","match","provider"):
                    self.delete(category,job["scope"])
                    if category=="privacy":
                        self.put("privacy","file:"+p["path"],{"mode":"hidden"})
                elif category in ("watch","pending","history") and account.get("id"):
                    self.db.execute("DELETE FROM records WHERE category=? AND scope LIKE ?",
                                    (category,str(account["id"])+"/%"))
                elif category=="cache":
                    self.db.execute("DELETE FROM records WHERE category='cache'")
                else:
                    raise AccountError("Nothing to clear for this account.")
                self.put("generation","data",{"id":secrets.token_hex(12)})
            self.previous=None
            self.reply(message="Local records cleared.",elapsed=0,proposal=None,results=[])
        elif op=="search":
            provider=job["provider"]
            generation=self.get("generation","data")
            query=job.get("query","")
            key=provider+"/"+hashlib.sha256((p["path"] if provider=="acoustid" else query).encode()).hexdigest()
            cached=self.get("cache",key)
            if cached.get("expires",0)>time.time():
                hits=cached["results"]
            else:
                if provider=="acoustid":
                    hits=fingerprint(p["path"],self.settings,self.throttle)
                else:
                    hits=candidates(provider,query,self.settings,self.throttle)
                with self.transaction():
                    self.context(job,True)
                    if generation!=self.get("generation","data"):
                        return
                    self.put("cache",key,{"expires":time.time()+(86400 if hits else 300),"results":hits})
            self.reply(results=hits,provider=provider,search_session=p["session"],message="Search complete.")
        elif op=="select":
            previous=self.get("response",self.window)
            if previous.get("search_session")!=p["session"] or job["candidate"] not in previous.get("results",[]):
                raise AccountError("Search again for this file.")
            se=re.search(r'\bS(\d+)E(\d+)\b',os.path.basename(p["path"]),re.I)
            hit=details(previous["provider"],job["candidate"],self.settings,self.throttle,se.groups() if se else None)
            self.context(job,True)
            scope={"file":"file:"+p["path"],"folder":"folder:"+os.path.dirname(p["path"]).replace("\\","/"),"global":"global"}[job["scope"]]
            with self.transaction():
                self.context(job,True)
                self.put("provider",scope,hit)
                self.put("match",scope,{"not_anime":True})
            self.reply(message="Match saved.")
        else:
            raise AccountError("Unknown action.")

    def sample(self):
        p=self.get("player",self.window)
        a=self.get("account","current")
        prev=self.previous
        self.previous=(p,a.get("generation"))
        if not prev or not a.get("id") or not p.get("active") or not p.get("safe_episode"):
            return
        before,generation=prev
        if not before.get("active") or generation!=a["generation"] or any(before.get(k)!=p.get(k)
                for k in ("session","media","episode","seek","speed","tracking")):
            return
        dt=p["time"]-before["time"]
        pos=p["pos"]-before["pos"]
        if not (0 < dt <= 2.5 and pos > 0 and abs(pos-dt*p["speed"]) < max(0.4,dt*p["speed"]*.3)):
            return
        if abs(time.time()*1000-p["wall"])>3000 or not re.fullmatch(r"[1-9]\d*",p["episode"]):
            return
        key=self.cycle(a,p)
        with self.transaction():
            watch=self.get("watch",key)
            if watch.get("owner") not in (None,self.window) and 0 <= p["time"]-watch.get("owner_time",0) < 2.75:
                return
            if watch.get("owner") not in (None,self.window):
                watch["end"] = p["time"]
            # Intersect intervals with the last credited endpoint across all windows.
            end=watch.get("end",0)
            if end > p["time"]: end=p["time"]
            start=max(before["time"],end)
            credit=min(dt,max(0,p["time"]-start))
            if self.get("history",key):
                return
            elapsed=watch.get("elapsed",0)+credit
            self.put("watch",key,dict(watch,elapsed=elapsed,end=p["time"],owner=self.window,owner_time=p["time"]))
        if not credit: return
        self.reply(elapsed=elapsed)
        mode=p["tracking"].get("mode","off")
        if mode not in ("ask","auto") or elapsed < 60*p["tracking"].get("threshold",15):
            return
        prop=self.get("pending",key)
        if prop:
            if prop["account"] == a["generation"] and prop["base"].get("session")==p["session"]:
                self.reply(proposal=prop)
            return
        if watch.get("retry",0)>time.time(): return
        job=dict(account=a["generation"],session=p["session"],media=p["media"])
        # Throttle reads after an offline failure or an unacknowledged write.
        with self.transaction():
            watch=self.get("watch",key); watch["retry"]=time.time()+60; self.put("watch",key,watch)
        entry=read_entry(self.credential(a),a["id"],p["media"])
        self.context(job,True)
        episode=int(p["episode"])
        if entry["progress"]>=episode:
            self.put("history",key,{"progress":entry["progress"]})
            return
        entry["session"]=p["session"]
        prop=dict(window=self.window,key=key,episode=episode,before=entry["progress"],base=entry,account=a["generation"])
        with self.transaction():
            if self.get("pending",key) or self.get("history",key): return
            self.put("pending",key,prop)
        if mode=="auto" and entry["id"] and entry["progress"]+1==episode and entry["status"]=="CURRENT":
            self.write(dict(job,base=entry,target=episode,automatic=True))
        else:
            self.reply(proposal=prop,message="Episode ready to review.")

    def run(self):
        while True:
            try:
                cancellation=self.get("cancel",self.window)
                if cancellation:
                    with self.transaction():
                        self.db.execute("DELETE FROM records WHERE category='pending' AND json_extract(value,'$.window')=? AND json_extract(value,'$.base.session')=?", (self.window,cancellation.get("session")))
                        self.db.execute("DELETE FROM records WHERE category='job' AND json_extract(value,'$.window')=? AND json_extract(value,'$.session')=?", (self.window,cancellation.get("session")))
                        self.delete("cancel",self.window)
                    self.previous=None
                
                with self.transaction():
                    row=self.db.execute("SELECT scope,value FROM records WHERE category='job' AND json_extract(value,'$.window')=? ORDER BY rowid LIMIT 1",(self.window,)).fetchone()
                    if row: self.delete("job",row[0])
                if row: self.handle(json.loads(row[1]))
                else:
                    p=self.get("player",self.window)
                    if not self.previous or p!=self.previous[0]:
                        self.sample()
            except AccountError as exc:
                self.reply(message=str(exc), offline=True)
            except Exception:
                self.reply(message="Operation failed. Check the connection and settings.")
            time.sleep(.25)


if __name__=="__main__":
    Worker(sys.argv[1],sys.argv[2],json.load(sys.stdin)).run()

