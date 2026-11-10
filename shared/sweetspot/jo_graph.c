/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 JUNGLE OS contributors */
#include "jo_graph.h"
#include "../seq/jo_seq.h" /* js_hash */

float jg_to_phys(const jg_param_t *p, float n)
{
    n = jo_clampf(n, 0.0f, 1.0f);
    switch (p->curve) {
    case JG_LOG: return p->lo * jo_exp2f(n * jo_log2f(p->hi / p->lo));
    case JG_DISCRETE: return jo_floorf(p->lo + n * (p->hi - p->lo) + 0.5f);
    default: return p->lo + n * (p->hi - p->lo);
    }
}

float jg_to_norm(const jg_param_t *p, float x)
{
    if (p->hi == p->lo) return 0.0f;
    if (p->curve == JG_LOG)
        return jo_clampf(jo_log2f(jo_maxf(x, p->lo) / p->lo) / jo_log2f(p->hi / p->lo), 0.0f, 1.0f);
    return jo_clampf((x - p->lo) / (p->hi - p->lo), 0.0f, 1.0f);
}

static void finish(const jg_graph_t *g, jg_eval_t *o)
{
    uint32_t i;
    for (i = 0; i < g->nparams; i++)
        o->phys[i] = jg_to_phys(&g->param[i], o->norm[i]);
}

void jg_eval_node(const jg_graph_t *g, uint32_t node, jg_eval_t *o)
{
    uint32_t i;
    const jg_node_t *nd = &g->node[node < g->nnodes ? node : 0u];
    for (i = 0; i < g->nparams; i++)
        o->norm[i] = nd->v[i];
    o->trim_db = nd->trim_db;
    o->seg = 0;
    o->near = (uint8_t)node;
    o->discrete_side = 0;
    finish(g, o);
}

float jg_path_t(const jg_graph_t *g, uint32_t k)
{
    if (g->npath < 2u) return 0.0f;
    return (float)(k < g->npath ? k : g->npath - 1u) / (float)(g->npath - 1u);
}

uint32_t jg_snap(const jg_graph_t *g, float t)
{
    if (g->npath < 2u) return 0u;
    return (uint32_t)(jo_clampf(t, 0.0f, 1.0f) * (float)(g->npath - 1u) + 0.5f);
}

void jg_eval(const jg_graph_t *g, float t, jg_eval_t *o)
{
    uint32_t seg, i;
    float f, s;
    const jg_node_t *a, *b;
    if (g->npath < 2u) {
        jg_eval_node(g, g->npath ? g->path[0] : 0u, o);
        return;
    }
    s = jo_clampf(t, 0.0f, 1.0f) * (float)(g->npath - 1u);
    seg = (uint32_t)s;
    if (seg >= g->npath - 1u) seg = g->npath - 2u;
    f = s - (float)seg;
    a = &g->node[g->path[seg]];
    b = &g->node[g->path[seg + 1u]];
    /* a smoothstep inside each segment: no slope corner at the nodes, which sit flat (easy to land on) */
    f = f * f * (3.0f - 2.0f * f);
    for (i = 0; i < g->nparams; i++) {
        const jg_param_t *p = &g->param[i];
        if (p->curve == JG_DISCRETE)
            o->norm[i] = f < 0.5f ? a->v[i] : b->v[i];
        else
            o->norm[i] = jo_lerpf(a->v[i], b->v[i], f); /* normalized space is already the curve's space */
    }
    o->trim_db = jo_lerpf(a->trim_db, b->trim_db, f);
    o->seg = (uint8_t)seg;
    o->near = f < 0.5f ? g->path[seg] : g->path[seg + 1u];
    o->discrete_side = (uint8_t)(f >= 0.5f);
    finish(g, o);
}

uint32_t jg_neighbours(const jg_graph_t *g, uint32_t node, uint8_t *out, uint32_t max)
{
    uint32_t i, n = 0;
    for (i = 0; i < g->nedges && n < max; i++) {
        if (g->edge[i].a == node) out[n++] = g->edge[i].b;
        else if (g->edge[i].b == node) out[n++] = g->edge[i].a;
    }
    return n;
}

int jg_edge_flags(const jg_graph_t *g, uint32_t a, uint32_t b)
{
    uint32_t i;
    for (i = 0; i < g->nedges; i++)
        if ((g->edge[i].a == a && g->edge[i].b == b) || (g->edge[i].a == b && g->edge[i].b == a))
            return g->edge[i].flags;
    return -1;
}

void jg_mutate(const jg_graph_t *g, const float *from, float *to, float radius, uint32_t seed)
{
    uint32_t i;
    radius = jo_clampf(radius, 0.0f, 1.0f);
    for (i = 0; i < g->nparams; i++) {
        const jg_param_t *p = &g->param[i];
        float slew = p->max_slew > 0.0f ? p->max_slew : 0.25f;
        float r = (float)(js_hash(seed, i, 0x5EEDu, 7u) >> 8) * (1.0f / 8388608.0f) - 1.0f; /* [-1, 1) */
        float d = r * radius * slew;
        if (p->curve == JG_DISCRETE) {
            float stepn = p->hi > p->lo ? 1.0f / (p->hi - p->lo) : 0.0f;
            d = radius >= 0.5f && jo_absf(r) > 0.6f ? (r > 0.0f ? stepn : -stepn) : 0.0f;
        }
        to[i] = jo_clampf(from[i] + d, 0.0f, 1.0f);
    }
}

void jg_history_push(jg_history_t *h, const float *v, uint32_t n)
{
    uint32_t i;
    h->head = (uint8_t)((h->head + 1u) % JG_HISTORY);
    for (i = 0; i < n && i < JG_MAXPARAMS; i++)
        h->v[h->head][i] = v[i];
    if (h->count < JG_HISTORY) h->count++;
}

int jg_history_pop(jg_history_t *h, float *v, uint32_t n)
{
    uint32_t i;
    if (!h->count) return -1;
    for (i = 0; i < n && i < JG_MAXPARAMS; i++)
        v[i] = h->v[h->head][i];
    h->head = (uint8_t)((h->head + JG_HISTORY - 1u) % JG_HISTORY);
    h->count--;
    return 0;
}

int jg_validate(const jg_graph_t *g)
{
    uint32_t i, k;
    if (g->nparams > JG_MAXPARAMS || g->nnodes > JG_MAXNODES || g->nnodes == 0) return 1;
    for (i = 0; i < g->nparams; i++) {
        const jg_param_t *p = &g->param[i];
        if (!(p->hi >= p->lo) || (p->curve == JG_LOG && p->lo <= 0.0f)) return 2;
    }
    for (k = 0; k < g->nnodes; k++) {
        for (i = 0; i < g->nparams; i++)
            if (!(g->node[k].v[i] >= 0.0f && g->node[k].v[i] <= 1.0f)) return 3;
        if (!(g->node[k].trim_db > -24.0f && g->node[k].trim_db < 12.0f)) return 4;
    }
    for (i = 0; i < g->nedges; i++)
        if (g->edge[i].a >= g->nnodes || g->edge[i].b >= g->nnodes) return 5;
    if (g->npath > JG_MAXPATH) return 6;
    for (i = 0; i < g->npath; i++)
        if (g->path[i] >= g->nnodes) return 6;
    /* each consecutive path pair must be an edge: MORPH only walks designed transitions */
    for (i = 0; i + 1u < g->npath; i++)
        if (jg_edge_flags(g, g->path[i], g->path[i + 1u]) < 0) return 7;
    return 0;
}
