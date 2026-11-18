// rmp_proto.cpp - see rmp_proto.h. Load-time / message code: RMP_COLD.
#include "rmp_proto.h"
#include <string.h>

RMP_COLD static int head(uint8_t *o, int cmd) {
  o[0] = 0xF0; o[1] = 0x7D; o[2] = 0x52; o[3] = 0x54; o[4] = (uint8_t)cmd; o[5] = RMP_PROTO_VER;
  return 6;
}
// n bytes of text: ASCII 0x20..0x7e (else '_'); the first NUL ends it (NUL padded from there on).
RMP_COLD static void text(uint8_t *o, const char *s, int n) {
  bool end = false;
  for (int i = 0; i < n; ++i) {
    uint8_t c = end ? 0 : (uint8_t)s[i];
    if (!c) end = true;
    o[i] = end ? 0 : (c >= 0x20 && c <= 0x7e) ? c : '_';
  }
}
RMP_COLD static bool ours(const uint8_t *m, int n, int cmd) {
  return n >= 7 && m[0] == 0xF0 && m[1] == 0x7D && m[2] == 0x52 && m[3] == 0x54 && m[4] == cmd &&
         m[5] == RMP_PROTO_VER && m[n - 1] == 0xF7;
}

RMP_COLD int rmp_proto_state(rmp_slot_state s) {
  switch (s) {
    case RMP_SLOT_READY: return RMP_PST_READY;
    case RMP_SLOT_LOADING: return RMP_PST_LOADING;
    case RMP_SLOT_MISSING: return RMP_PST_MISSING;
    case RMP_SLOT_BAD: return RMP_PST_BAD;
    case RMP_SLOT_NO_ROOM: return RMP_PST_NO_ROOM;
    case RMP_SLOT_MUTED: return RMP_PST_MUTED;
    default: return RMP_PST_EMPTY;
  }
}

RMP_COLD uint16_t rmp_plugin_fp(const char *id) {
  uint32_t h = 0x811C9DC5u;
  for (; *id; ++id) h = (h ^ (uint8_t)*id) * 16777619u;
  return (uint16_t)(h % 16383u + 1u);
}

RMP_COLD bool rmp_parse_ask(const uint8_t *m, int n, rmp_ask *a) {
  if (n != 11 || !ours(m, n, RMP_CMD_ASK) || m[6] > 5 || m[8] > RMP_ASK_INSTALLED) return false;
  if ((m[8] == RMP_ASK_INSTALLED) != (m[6] == 0)) return false;   // INSTALLED: slot 0 only; the others: slot 1..5
  a->slot = m[6]; a->gen = m[7] & 0x7F; a->what = m[8]; a->arg = m[9] & 0x7F;
  return true;
}
RMP_COLD bool rmp_parse_assign(const uint8_t *m, int n, rmp_assign *a) {
  if (n != 11 || !ours(m, n, RMP_CMD_ASSIGN) || m[6] < 1 || m[6] > 5 || m[7] > RMP_OP_INSTALL) return false;
  a->slot = m[6]; a->op = m[7]; a->arg = m[8] & 0x7F; a->installed_gen = m[9] & 0x7F;
  return true;
}

RMP_COLD int rmp_meta_nparts(const rmp_desc *d) {
  return 1 + (d ? (d->nknobs + RMP_KNOBS_PER_PART - 1) / RMP_KNOBS_PER_PART : 0);
}
RMP_COLD int rmp_build_meta_unchanged(uint8_t *out, int slot, int gen) {
  int i = head(out, RMP_CMD_META);
  out[i++] = (uint8_t)(slot & 0x7F); out[i++] = (uint8_t)(gen & 0x7F); out[i++] = RMP_META_UNCHANGED; out[i++] = 0xF7;
  return i;
}
RMP_COLD int rmp_build_meta_head(uint8_t *out, int slot, int gen, int state, int flags, const rmp_desc *d, int fp) {
  return rmp_build_meta_head_why(out, slot, gen, state, flags, d, fp, RMP_WHY_NONE);
}
RMP_COLD const char *rmp_why_tag(int why) {
  static const char *const tag[] = {"", "ROOM", "CRSH", "TIME", "MISS", "BAD", "SAFE"};
  return why > 0 && why <= RMP_WHY_SAFE ? tag[why] : "";
}
RMP_COLD int rmp_why_of(int pstate, int muted) {
  switch (pstate) {
    case RMP_PST_NO_ROOM: return RMP_WHY_ROOM;
    case RMP_PST_MISSING: return RMP_WHY_MISS;
    case RMP_PST_BAD: return RMP_WHY_BAD;
    case RMP_PST_MUTED: return muted > 0 && muted <= RMP_WHY_SAFE ? muted : RMP_WHY_CRASH;
    default: return RMP_WHY_NONE;
  }
}
RMP_COLD void rmp_why_name(char out[10], int why, const rmp_desc *d) {
  const char *t = rmp_why_tag(why);
  int n = 0;
  for (; t[n] && n < 4; ++n) out[n] = t[n];
  if (n && d) {
    const char *s = d->short_name[0] ? d->short_name : d->name;
    int k = 0;
    while (k < 4 && s[k] == ' ') ++k;          // a short name never starts with a space; a name might
    if (s[k]) {
      out[n++] = ' ';
      for (int e = k + 4; s[k] && k < e; ++k) out[n++] = s[k];
      while (n > 0 && out[n - 1] == ' ') --n;
    }
  }
  out[n] = 0;
}
RMP_COLD int rmp_build_meta_head_why(uint8_t *out, int slot, int gen, int state, int flags, const rmp_desc *d, int fp,
                                     int why) {
  char nm[10];
  rmp_why_name(nm, why, d);
  int i = head(out, RMP_CMD_META);
  out[i++] = (uint8_t)(slot & 0x7F); out[i++] = (uint8_t)(gen & 0x7F); out[i++] = 0;
  out[i++] = (uint8_t)(state & 0x7F); out[i++] = (uint8_t)(flags & 7);
  out[i++] = d ? (uint8_t)(d->nknobs & 0x7F) : 0; out[i++] = (uint8_t)rmp_meta_nparts(d);
  text(out + i, nm[0] ? nm : d ? d->name : "", 10); i += 10;
  text(out + i, d ? d->short_name : "", 4); i += 4;
  text(out + i, d ? d->p2_title : "", 13); i += 13;
  if ((state & 0x7F) != RMP_PST_READY) fp = 0;   // only a READY slot carries its fingerprint
  out[i++] = (uint8_t)(fp >> 7 & 0x7F); out[i++] = (uint8_t)(fp & 0x7F);
  out[i++] = 0xF7;
  return i;
}
RMP_COLD int rmp_build_meta_part(uint8_t *out, int slot, int gen, int part, const rmp_desc *d) {
  if (!d || part < 1 || part >= rmp_meta_nparts(d)) return 0;
  int i = head(out, RMP_CMD_META);
  out[i++] = (uint8_t)(slot & 0x7F); out[i++] = (uint8_t)(gen & 0x7F); out[i++] = (uint8_t)(part & 0x7F);
  int first = (part - 1) * RMP_KNOBS_PER_PART;
  for (int k = first; k < d->nknobs && k < first + RMP_KNOBS_PER_PART; ++k) {
    const rmp_desc_knob &q = d->knob[k];
    out[i++] = q.p & 0x7F; out[i++] = q.look & 0x7F; out[i++] = q.kind & 0x7F; out[i++] = q.role & 0x7F;
    out[i++] = q.min & 0x7F; out[i++] = q.max & 0x7F; out[i++] = q.def & 0x7F; out[i++] = q.enum_count & 0x7F;
    text(out + i, q.long_name, 12); i += 12;
    text(out + i, q.short_name, 4); i += 4;
  }
  out[i++] = 0xF7;
  return i;
}
RMP_COLD int rmp_desc_pad_unused(rmp_desc *d) {
  if (!d) return 0;
  int top = d->page2 ? 16 : 8, added = 0, pre_p = d->pre >= 0 && d->pre < d->nknobs ? d->knob[d->pre].p : 0;
  for (int p = 1; p <= top && d->nknobs < RMP_DESC_KNOBS; ++p) {
    int at = 0;
    while (at < d->nknobs && d->knob[at].p < p) ++at;
    if (at < d->nknobs && d->knob[at].p == p) continue;
    memmove(&d->knob[at + 1], &d->knob[at], (size_t)(d->nknobs - at) * sizeof d->knob[0]);
    memset(&d->knob[at], 0, sizeof d->knob[0]);
    d->knob[at].p = (uint8_t)p;
    d->nknobs++, added++;
  }
  if (pre_p)
    for (int k = 0; k < d->nknobs; ++k)
      if (d->knob[k].p == pre_p) d->pre = (int8_t)k;
  return added;
}
RMP_COLD int rmp_build_list(uint8_t *out, int slot, int list, int gen, int replace, int total, int first, int count,
                            const char *names, int stride) {
  int i = head(out, RMP_CMD_LIST);
  out[i++] = (uint8_t)(slot & 0x7F); out[i++] = (uint8_t)(list & 0x7F); out[i++] = (uint8_t)(gen & 0x7F);
  out[i++] = replace ? 1 : 0; out[i++] = (uint8_t)(total < 0 ? 0 : total > 127 ? 127 : total);
  for (int e = 0; e < count && e < RMP_LIST_PER_MSG; ++e) {
    out[i++] = (uint8_t)((first + e) & 0x7F);
    text(out + i, names + e * stride, 13); i += 13;
  }
  out[i++] = 0xF7;
  return i;
}
RMP_COLD int rmp_build_knobs(uint8_t *out, int voice, int slot, int n, const uint8_t *knob, const uint8_t *val) {
  int i = head(out, RMP_CMD_KNOBS);
  if (n > 16) n = 16;
  if (n < 0) n = 0;
  out[i++] = (uint8_t)(voice & 7); out[i++] = (uint8_t)(slot & 0x7F); out[i++] = (uint8_t)n;
  for (int k = 0; k < n; ++k) { out[i++] = knob[k] & 0x0F; out[i++] = val[k] & 0x7F; }
  out[i++] = 0xF7;
  return i;
}
RMP_COLD int rmp_build_slots(uint8_t *out, const uint8_t gen[5], int installed_gen, int slot, int result) {
  int i = head(out, RMP_CMD_SLOTS);
  for (int s = 0; s < 5; ++s) out[i++] = gen[s] & 0x7F;
  out[i++] = (uint8_t)(installed_gen & 0x7F);
  if (slot) { out[i++] = (uint8_t)(slot & 0x7F); out[i++] = (uint8_t)(result & 0x7F); }
  out[i++] = 0xF7;
  return i;
}

// ---- 52 UM PAGES (rmp_proto.h)
RMP_COLD static int pg_head(uint8_t *o, int kind) {
  int i = head(o, RMP_CMD_PAGES);
  o[5] = RMP_PAGES_VER;
  o[i++] = (uint8_t)kind;
  return i;
}
RMP_COLD int rmp_build_pg_head(uint8_t *out, int slot, int gen, const rmp_pages *pg) {
  bool on = pg && pg->present;
  int i = pg_head(out, RMP_PG_HEAD), pages = 0, last = 0;
  for (int k = 0; on && k < pg->nhi; ++k) {
    pages |= pg->hi[k].p <= 24 ? 1 : 2;
    if (pg->hi[k].p > last) last = pg->hi[k].p;
  }
  out[i++] = (uint8_t)(slot & 0x7F); out[i++] = (uint8_t)(gen & 0x7F);
  out[i++] = on ? pg->nhi : 0; out[i++] = (uint8_t)pages; out[i++] = (uint8_t)last; out[i++] = on ? pg->nstep : 0;
  text(out + i, on ? pg->p3_title : "", 13); i += 13;
  text(out + i, on ? pg->p4_title : "", 13); i += 13;
  out[i++] = 0xF7;
  return i;
}
RMP_COLD int rmp_build_pg_knobs(uint8_t *out, int slot, int gen, const rmp_pages *pg, int part) {
  int first = part * RMP_PG_KNOBS_PER_MSG;
  if (!pg || !pg->present || part < 0 || first >= pg->nhi) return 0;
  int n = pg->nhi - first < RMP_PG_KNOBS_PER_MSG ? pg->nhi - first : RMP_PG_KNOBS_PER_MSG;
  int i = pg_head(out, RMP_PG_KNOBS);
  out[i++] = (uint8_t)(slot & 0x7F); out[i++] = (uint8_t)(gen & 0x7F); out[i++] = (uint8_t)n;
  for (int k = first; k < first + n; ++k) {
    const rmp_desc_knob &q = pg->hi[k];
    out[i++] = q.p & 0x7F; out[i++] = q.look & 0x7F; out[i++] = q.kind & 0x7F; out[i++] = q.role & 0x7F;
    out[i++] = q.min & 0x7F; out[i++] = q.max & 0x7F; out[i++] = q.def & 0x7F; out[i++] = q.enum_count & 0x7F;
    text(out + i, q.long_name, 12); i += 12;
    text(out + i, q.short_name, 4); i += 4;
  }
  out[i++] = 0xF7;
  return i;
}
RMP_COLD int rmp_build_pg_steps(uint8_t *out, int slot, int gen, const rmp_pages *pg, int part) {
  int first = part * RMP_PG_STEPS_PER_MSG;
  if (!pg || !pg->present || part < 0 || first >= pg->nstep) return 0;
  int n = pg->nstep - first < RMP_PG_STEPS_PER_MSG ? pg->nstep - first : RMP_PG_STEPS_PER_MSG;
  int i = pg_head(out, RMP_PG_STEPS);
  out[i++] = (uint8_t)(slot & 0x7F); out[i++] = (uint8_t)(gen & 0x7F); out[i++] = (uint8_t)n;
  for (int k = first; k < first + n; ++k) {
    const rmp_step_desc &s = pg->step[k];
    out[i++] = s.p & 0x7F; out[i++] = s.kind & 0x7F;
    out[i++] = (uint8_t)(s.steps >> 7 & 0x7F); out[i++] = (uint8_t)(s.steps & 0x7F);
    out[i++] = (uint8_t)(s.def14 >> 7 & 0x7F); out[i++] = (uint8_t)(s.def14 & 0x7F);
    out[i++] = s.kind == RMP_STEP_PAIR ? (uint8_t)(s.name_count & 0x7F) : 0;
  }
  out[i++] = 0xF7;
  return i;
}
RMP_COLD int rmp_build_pg_pair(uint8_t *out, int slot, int gen, int p, const char (*names)[8], int total, int first,
                               int count) {
  if (!names || total < 2 || total > RMP_ENUM_MAX || first < 0 || first >= total || count < 1) return 0;
  if (count > RMP_PG_PAIR_PER_MSG) count = RMP_PG_PAIR_PER_MSG;
  if (count > total - first) count = total - first;
  int i = pg_head(out, RMP_PG_PAIR);
  out[i++] = (uint8_t)(slot & 0x7F); out[i++] = (uint8_t)(gen & 0x7F); out[i++] = (uint8_t)(p & 0x7F);
  out[i++] = (uint8_t)total; out[i++] = (uint8_t)first; out[i++] = (uint8_t)count;
  for (int k = first; k < first + count; ++k) { text(out + i, names[k], RMP_PG_NAME_CHARS); i += RMP_PG_NAME_CHARS; }
  out[i++] = 0xF7;
  return i;
}
RMP_COLD int rmp_build_pg_values(uint8_t *out, int voice, int slot, int first, int n, const uint8_t *v) {
  if (first < RMP_PAGES_P_FIRST || n < 1 || first + n - 1 > RMP_PAGES_P_LAST) return 0;
  int i = pg_head(out, RMP_PG_VALUES);
  out[i++] = (uint8_t)(voice & 7); out[i++] = (uint8_t)(slot & 0x7F); out[i++] = (uint8_t)first; out[i++] = (uint8_t)n;
  for (int k = 0; k < n; ++k) out[i++] = v[k] & 0x7F;
  out[i++] = 0xF7;
  return i;
}
RMP_COLD static bool pg_ours(const uint8_t *m, int n, int kind) {
  return n >= 8 && m[0] == 0xF0 && m[1] == 0x7D && m[2] == 0x52 && m[3] == 0x54 && m[4] == RMP_CMD_PAGES &&
         m[5] == RMP_PAGES_VER && m[6] == kind && m[n - 1] == 0xF7;
}
RMP_COLD bool rmp_parse_pg_values(const uint8_t *m, int n, rmp_pg_values *x) {
  if (!pg_ours(m, n, RMP_PG_VALUES) || n < 12) return false;
  int c = m[10];
  if (m[7] > 7 || m[8] < 1 || m[8] > 5 || m[9] < RMP_PAGES_P_FIRST || c < 1 || m[9] + c - 1 > RMP_PAGES_P_LAST ||
      n != 12 + c)
    return false;
  x->voice = m[7], x->slot = m[8], x->first = m[9], x->n = (uint8_t)c;
  for (int k = 0; k < c; ++k) {
    if (m[11 + k] > 127) return false;
    x->v[k] = m[11 + k];
  }
  return true;
}
RMP_COLD bool rmp_parse_pg_ask(const uint8_t *m, int n, int *slot) {
  if (!pg_ours(m, n, RMP_PG_ASK) || n != 9 || m[7] > 5) return false;
  *slot = m[7];
  return true;
}
