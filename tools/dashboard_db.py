"""SQLite archive for device snapshots, card activity, and balance changes."""

import json
import sqlite3
import time
from contextlib import closing


class Archive:
    def __init__(self, path):
        path.parent.mkdir(parents=True, exist_ok=True)
        self.path = path
        with closing(self.connect()) as db:
            db.execute("PRAGMA journal_mode=WAL")
            db.executescript("""
                CREATE TABLE IF NOT EXISTS devices (
                    id TEXT PRIMARY KEY, observed REAL NOT NULL, snapshot TEXT NOT NULL
                );
                CREATE TABLE IF NOT EXISTS accounts (
                    device TEXT NOT NULL, uid TEXT NOT NULL, balance INTEGER NOT NULL,
                    updated REAL NOT NULL, PRIMARY KEY(device, uid)
                );
                CREATE TABLE IF NOT EXISTS events (
                    id INTEGER PRIMARY KEY, device TEXT NOT NULL, boot TEXT NOT NULL,
                    seq INTEGER NOT NULL, observed REAL NOT NULL, device_ms INTEGER NOT NULL,
                    type TEXT NOT NULL, uid TEXT NOT NULL, amount INTEGER NOT NULL, text TEXT NOT NULL,
                    UNIQUE(device, boot, seq)
                );
                CREATE INDEX IF NOT EXISTS events_card ON events(device, uid, id DESC);
                CREATE INDEX IF NOT EXISTS events_device ON events(device, id DESC);
                CREATE TABLE IF NOT EXISTS card_labels (
                    device TEXT NOT NULL, uid TEXT NOT NULL, label TEXT NOT NULL,
                    PRIMARY KEY(device, uid)
                );
                CREATE TABLE IF NOT EXISTS streams (
                    device TEXT NOT NULL, boot TEXT NOT NULL, last_seq INTEGER NOT NULL,
                    missing INTEGER NOT NULL, PRIMARY KEY(device, boot)
                );
            """)

    def connect(self):
        db = sqlite3.connect(self.path)
        db.row_factory = sqlite3.Row
        return db

    def ingest(self, device, snapshot, observed=None):
        observed = observed if observed is not None else time.time()
        state, boot = snapshot["state"], snapshot["bootId"]
        events = state["events"]
        with closing(self.connect()) as db, db:
            db.execute("""
                INSERT INTO devices VALUES (?, ?, ?)
                ON CONFLICT(id) DO UPDATE SET observed=excluded.observed, snapshot=excluded.snapshot
            """, (device, observed, json.dumps(snapshot, ensure_ascii=False)))

            if state["storageHealthy"]:
                db.executemany("""
                    INSERT INTO accounts VALUES (?, ?, ?, ?)
                    ON CONFLICT(device, uid) DO UPDATE SET balance=excluded.balance, updated=excluded.updated
                """, [(device, a["uid"], a["balance"], observed) for a in state["accounts"]])
                # Starting a new simulated scene has a new set of accounts. Keep its earlier events.
                removed = set(row["uid"] for row in db.execute(
                    "SELECT uid FROM accounts WHERE device=?", (device,)
                )) - {a["uid"] for a in state["accounts"]}
                db.executemany("DELETE FROM accounts WHERE device=? AND uid=?", [(device, uid) for uid in removed])

            if not events:
                return
            cursor = db.execute("SELECT last_seq, missing FROM streams WHERE device=? AND boot=?", (device, boot)).fetchone()
            last, missing = (cursor["last_seq"], cursor["missing"]) if cursor else (0, 0)
            new = [event for event in events if event["seq"] > last]
            for event in new:
                seq = event["seq"]
                missing += max(0, seq - last - 1)
                last = seq
                db.execute("""
                    INSERT INTO events(device, boot, seq, observed, device_ms, type, uid, amount, text)
                    VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
                """, (device, boot, seq, observed, event["t"], event["type"],
                      event["uid"], event["amount"], event["text"]))
            db.execute("""
                INSERT INTO streams VALUES (?, ?, ?, ?)
                ON CONFLICT(device, boot) DO UPDATE SET last_seq=excluded.last_seq, missing=excluded.missing
            """, (device, boot, last, missing))

    def latest(self, device):
        with closing(self.connect()) as db:
            row = db.execute("SELECT observed, snapshot FROM devices WHERE id=?", (device,)).fetchone()
        return (row["observed"], json.loads(row["snapshot"])) if row else (None, None)

    def set_card_label(self, device, uid, label):
        with closing(self.connect()) as db, db:
            tapped = db.execute(
                "SELECT 1 FROM events WHERE device=? AND uid=? AND type='card_tap' LIMIT 1",
                (device, uid),
            ).fetchone()
            if tapped is None:
                raise ValueError("請先用這張卡刷一下")
            db.execute("""
                INSERT INTO card_labels VALUES (?, ?, ?)
                ON CONFLICT(device, uid) DO UPDATE SET label=excluded.label
            """, (device, uid, label))

    def view(self, device, card, category, before):
        with closing(self.connect()) as db:
            db.execute("BEGIN")
            row = db.execute("SELECT observed, snapshot FROM devices WHERE id=?", (device,)).fetchone()
            observed, snapshot = (row["observed"], json.loads(row["snapshot"])) if row else (None, None)
            accounts = [dict(row) for row in db.execute(
                "SELECT uid, balance FROM accounts WHERE device=? ORDER BY uid", (device,)
            )]
            cards = [row["uid"] for row in db.execute("""
                SELECT uid FROM accounts WHERE device=?
                UNION SELECT uid FROM events WHERE device=? AND uid != ''
                ORDER BY uid
            """, (device, device))]
            if card not in cards:
                card = cards[0] if cards else None
            card_labels = dict(db.execute("SELECT uid, label FROM card_labels WHERE device=?", (device,)))

            clauses, params = ["device=?"], [device]
            if category != "device":
                clauses.append("uid=?")
                params.append(card)
            if category == "money":
                clauses.append("type IN ('penalty', 'reward')")
            elif category == "rides":
                clauses.append("type IN ('card_tap', 'rent', 'return', 'item_left', 'pending')")
            if before:
                clauses.append("id < ?")
                params.append(before)
            history = [dict(row) for row in db.execute(f"""
                SELECT id, uid, observed, type, amount, text FROM events
                WHERE {' AND '.join(clauses)} ORDER BY id DESC LIMIT 21
            """, params)]
            last_tap = db.execute("""
                SELECT uid, observed FROM events
                WHERE device=? AND type='card_tap' ORDER BY id DESC LIMIT 1
            """, (device,)).fetchone()
            missing = db.execute("SELECT COALESCE(SUM(missing), 0) FROM streams WHERE device=?", (device,)).fetchone()[0]

        state = snapshot["state"] if snapshot else None
        if state is not None:
            state["accounts"] = accounts
            state.pop("events", None)
        return {
            "observed": observed, "state": state, "hardware": snapshot["hardware"] if snapshot else {},
            "signals": snapshot.get("signals", {}) if snapshot else {},
            "manualLid": bool(snapshot and snapshot.get("manualLid")),
            "lastTap": dict(last_tap) if last_tap else None,
            "cardLabels": card_labels,
            "archiveGaps": missing, "cards": cards, "card": card, "events": history[:20],
            "nextBefore": history[19]["id"] if len(history) > 20 else None,
        }
