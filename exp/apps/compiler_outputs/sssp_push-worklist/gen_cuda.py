from gg.ast import *
from gg.lib.graph import Graph
from gg.lib.wl import Worklist
from gg.ast.params import GraphParam
import cgen
G = Graph("graph")
WL = Worklist()
ast = Module([
CBlock([cgen.Include("kernels/reduce.cuh", system = False)], parse = False),
CBlock([cgen.Include("gen_cuda.cuh", system = False)], parse = False),
CDeclGlobal([("unsigned int *", "P_DIST_CURRENT", "")]),
Kernel("InitializeGraph", [G.param(), ('int ', 'nowned') , ('unsigned int', 'local_infinity'), ('unsigned int', 'local_src_node'), ('unsigned int *', 'p_dist_current')],
[
ForAll("src", G.nodes(None, "nowned"),
[
CBlock(["p_dist_current[src] = (graph.node_data[src] == local_src_node) ? 0 : local_infinity"]),
]),
]),
Kernel("SSSP", [G.param(), ('int ', 'nowned') , ('unsigned int *', 'p_dist_current')],
[
ForAll("wlvertex", WL.items(),
[
CDecl([("int", "src", "")]),
CDecl([("bool", "pop", "")]),
WL.pop("pop", "wlvertex", "src"),
CDecl([("unsigned int", "sdist", "")]),
CBlock(["sdist = p_dist_current[src]"]),
ClosureHint(
ForAll("jj", G.edges("src"),
[
CDecl([("index_type", "dst", "")]),
CBlock(["dst = graph.getAbsDestination(jj)"]),
CDecl([("unsigned int", "new_dist", "")]),
CBlock(["new_dist = graph.getAbsWeight(jj) + sdist"]),
CDecl([("unsigned int", "old_dist", "")]),
CBlock(["old_dist = atomicMin(&p_dist_current[dst], new_dist)"]),
If("old_dist > new_dist",
[
WL.push("dst"),
]),
]),
),
]),
]),
Kernel("InitializeGraph_cuda", [('unsigned int', 'local_src_node'), ('unsigned int', 'local_infinity'), ('struct CUDA_Context *', 'ctx')],
[
CDecl([("dim3", "blocks", "")]),
CDecl([("dim3", "threads", "")]),
CBlock(["kernel_sizing(ctx->gg, blocks, threads)"]),
Invoke("InitializeGraph", ("ctx->gg", "ctx->nowned", "local_infinity", "local_src_node", "ctx->dist_current.gpu_wr_ptr()")),
CBlock(["check_cuda_kernel"], parse = False),
], host = True),
Kernel("SSSP_cuda", [('struct CUDA_Context *', 'ctx')],
[
CDecl([("dim3", "blocks", "")]),
CDecl([("dim3", "threads", "")]),
CBlock(["kernel_sizing(ctx->gg, blocks, threads)"]),
CBlock(["ctx->in_wl.update_gpu(ctx->shared_wl->num_in_items)"]),
CBlock(["ctx->out_wl.will_write()"]),
CBlock(["ctx->out_wl.reset()"]),
Invoke("SSSP", ("ctx->gg", "ctx->nowned", "ctx->dist_current.gpu_wr_ptr()", "ctx->in_wl", "ctx->out_wl")),
CBlock(["check_cuda_kernel"], parse = False),
CBlock(["ctx->out_wl.update_cpu()"]),
CBlock(["ctx->shared_wl->num_out_items = ctx->out_wl.nitems()"]),
], host = True),
])
