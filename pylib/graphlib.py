# graphlib.TopologicalSorter
__all__ = ["TopologicalSorter", "CycleError"]

_NODE_OUT = -1
_NODE_DONE = -2


class _NodeInfo:
    def __init__(self, node):
        self.node = node
        self.npredecessors = 0
        self.successors = []


class CycleError(ValueError):
    pass


class TopologicalSorter:
    def __init__(self, graph=None):
        self._node2info = {}
        self._ready_nodes = None
        self._npassedout = 0
        self._nfinished = 0
        if graph is not None:
            for node, predecessors in graph.items():
                self.add(node, *predecessors)

    def _get_nodeinfo(self, node):
        result = self._node2info.get(node)
        if result is None:
            self._node2info[node] = result = _NodeInfo(node)
        return result

    def add(self, node, *predecessors):
        if self._ready_nodes is not None:
            raise ValueError("Nodes cannot be added after a call to prepare()")
        nodeinfo = self._get_nodeinfo(node)
        nodeinfo.npredecessors += len(predecessors)
        for pred in predecessors:
            pred_info = self._get_nodeinfo(pred)
            pred_info.successors.append(node)

    def prepare(self):
        if self._ready_nodes is not None:
            raise ValueError("cannot prepare() more than once")
        self._ready_nodes = [i.node for i in self._node2info.values() if i.npredecessors == 0]
        cycle = self._find_cycle()
        if cycle:
            raise CycleError("nodes are in a cycle", cycle)

    def get_ready(self):
        if self._ready_nodes is None:
            raise ValueError("prepare() must be called first")
        result = tuple(self._ready_nodes)
        n2i = self._node2info
        for node in result:
            n2i[node].npredecessors = _NODE_OUT
        self._ready_nodes.clear()
        self._npassedout += len(result)
        return result

    def is_active(self):
        if self._ready_nodes is None:
            raise ValueError("prepare() must be called first")
        return self._nfinished < self._npassedout or bool(self._ready_nodes)

    def __bool__(self):
        return self.is_active()

    def done(self, *nodes):
        if self._ready_nodes is None:
            raise ValueError("prepare() must be called first")
        n2i = self._node2info
        for node in nodes:
            nodeinfo = n2i.get(node)
            if nodeinfo is None:
                raise ValueError(f"node {node!r} was not added using add()")
            stat = nodeinfo.npredecessors
            if stat != _NODE_OUT:
                if stat >= 0:
                    raise ValueError(f"node {node!r} was not passed out (still not ready)")
                elif stat == _NODE_DONE:
                    raise ValueError(f"node {node!r} was already marked done")
                else:
                    assert False, f"node {node!r}: unknown status {stat}"
            nodeinfo.npredecessors = _NODE_DONE
            for successor in nodeinfo.successors:
                successor_info = n2i[successor]
                successor_info.npredecessors -= 1
                if successor_info.npredecessors == 0:
                    self._ready_nodes.append(successor)
            self._nfinished += 1

    def _find_cycle(self):
        n2i = self._node2info
        stack = []
        itstack = []
        seen = set()
        node2stacki = {}
        for node in n2i:
            if node in seen:
                continue
            while True:
                if node in seen:
                    if node in node2stacki:
                        return stack[node2stacki[node]:] + [node]
                else:
                    seen.add(node)
                    itstack.append(iter(n2i[node].successors).__next__)
                    node2stacki[node] = len(stack)
                    stack.append(node)
                while stack:
                    try:
                        node = itstack[-1]()
                        break
                    except StopIteration:
                        del node2stacki[stack.pop()]
                        itstack.pop()
                else:
                    break
        return None

    def static_order(self):
        self.prepare()
        while self.is_active():
            node_group = self.get_ready()
            yield from node_group
            self.done(*node_group)
