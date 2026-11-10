/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 JUNGLE OS contributors */
/* The -O2 unit (fm1.toml o2): the shared engines and Break Surgeon's, compiled apart from the -Os app. */
#include "../../shared/dsp/jo_dsp.h"
#include "../../shared/seq/jo_seq.c"
#include "../../shared/drums/jo_drums.c"
#include "../../shared/sampler/jo_break.c"
#include "../../shared/fx/jo_fx.c"
#include "../../shared/sweetspot/jo_graph.c"
#include "../../shared/link/jo_link.c"
#include "../../shared/store/jo_store.c"
#include "engine/bs_engine.c"
