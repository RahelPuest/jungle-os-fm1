/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 JUNGLE OS contributors */
/* The Sweetspot Graph: curated sound (or rhythm) states and the ways between them.
 *
 * A model declares its parameters (jg_param_t: range, curve, whether it may be interpolated). A graph holds up
 * to JG_MAXNODES nodes, each a complete normalized parameter vector with a gain trim, and edges between them.
 * MORPH walks a designed path (a node sequence) with one macro: within a segment the continuous parameters
 * interpolate in their own curve's space (log for frequencies and times), the discrete ones switch at the
 * segment's midpoint, which the caller covers with a short crossfade (jg_eval reports it). The trims
 * interpolate in dB, so a morph holds its loudness the way the nodes were trimmed.
 *
 * Every node carries a review flag: JG_REVIEWED only after listening and the measured checks
 * (docs/development/sweetspots.md). Mutation candidates are created unreviewed and never overwrite nodes. */
#pragma once
#include "../dsp/jo_math.h"

#define JG_MAXPARAMS 18
#define JG_MAXNODES 16
#define JG_MAXEDGES 32
#define JG_MAXPATH 8
#define JG_HISTORY 8

enum { JG_LIN, JG_LOG, JG_DISCRETE };
enum { JG_UNREVIEWED = 0, JG_REVIEWED = 1 };

typedef struct {
    const char *name;
    float lo, hi;        /* physical range; JG_LOG needs lo > 0 */
    uint8_t curve;       /* JG_LIN, JG_LOG, JG_DISCRETE (rounded to an integer in [lo, hi]) */
    float max_slew;      /* largest normalized change per mutation step (0: 0.25) */
} jg_param_t;

typedef struct {
    const char *name;
    float v[JG_MAXPARAMS]; /* normalized 0..1 */
    float trim_db;         /* loudness trim of this node */
    uint8_t review;
    uint8_t tags;          /* model-specific bits (aggressive, wide, clean ...) */
} jg_node_t;

typedef struct { uint8_t a, b, flags; } jg_edge_t; /* flags: JG_EDGE_XFADE (must crossfade) */
#define JG_EDGE_XFADE 1u

typedef struct {
    const jg_param_t *param;
    uint8_t nparams;
    jg_node_t node[JG_MAXNODES];
    uint8_t nnodes;
    jg_edge_t edge[JG_MAXEDGES];
    uint8_t nedges;
    uint8_t path[JG_MAXPATH]; /* MORPH's path through the nodes */
    uint8_t npath;
} jg_graph_t;

/* the result of an evaluation */
typedef struct {
    float norm[JG_MAXPARAMS];  /* normalized */
    float phys[JG_MAXPARAMS];  /* physical, through each parameter's curve */
    float trim_db;
    uint8_t seg, near;         /* the segment, and the nearest path node (as a node index) */
    uint8_t discrete_side;     /* 0: the segment's first node's discrete values, 1: the second's */
} jg_eval_t;

float jg_to_phys(const jg_param_t *p, float norm);
float jg_to_norm(const jg_param_t *p, float phys);
/* MORPH: t in [0, 1] along the path */
void jg_eval(const jg_graph_t *g, float t, jg_eval_t *out);
/* a node's own vector, as an evaluation (SNAP, EXPLORE) */
void jg_eval_node(const jg_graph_t *g, uint32_t node, jg_eval_t *out);
/* the path position t of a path node (SNAP: MORPH's knob jumps there) */
float jg_path_t(const jg_graph_t *g, uint32_t path_index);
/* the nearest path node to t */
uint32_t jg_snap(const jg_graph_t *g, float t);
/* neighbours of node through the edges: up to max node indices, returns the count */
uint32_t jg_neighbours(const jg_graph_t *g, uint32_t node, uint8_t *out, uint32_t max);
int jg_edge_flags(const jg_graph_t *g, uint32_t a, uint32_t b); /* -1: no edge */

/* bounded mutation: a new vector within radius (0..1) of `from`, each parameter moved at most radius x its
 * max_slew, kept in [0, 1]; discrete parameters move only with radius >= 0.5. Deterministic for a seed */
void jg_mutate(const jg_graph_t *g, const float *from, float *to, float radius, uint32_t seed);

/* a small undo history of vectors */
typedef struct {
    float v[JG_HISTORY][JG_MAXPARAMS];
    uint8_t head, count;
} jg_history_t;
void jg_history_push(jg_history_t *h, const float *v, uint32_t n);
int jg_history_pop(jg_history_t *h, float *v, uint32_t n); /* 0 = restored, -1 = empty */

/* sanity of a graph: ranges, edges and the path within bounds; 0 = good, else the first problem's code */
int jg_validate(const jg_graph_t *g);
