from dataclasses import dataclass
from pprint import pprint

from middlewared.utils import filters, get_impl
from middlewared.service_exception import MatchNotFound
from truenas_pylibzfs import open_handle, ZFSProperty

generic_filters = filters()


@dataclass(slots=True, kw_only=True)
class QueryFiltersCallbackState:
    iterator_fn_name: str  # the name of function doing the iteration(s)
    filters: list  # query-filters
    filter_fn: callable  # function to do filtering
    get_fn: callable  # function to get value from dict
    select_fn: callable  # function to select values
    select: list | None = None  # list of fields to select. None means all.
    single_result: bool = False  # return single result with no pagination
    count_only: bool = False  # only count entries
    recursive: bool = False  # recursively iterate
    propset: set[ZFSProperty] | None = None  # zfs properties to retrieve
    results: list = None
    count: int = 0


def generic_query_callback(hdl, state):
    data = hdl.asdict(properties=state.propset)
    for f in state.filters:
        if not state.filter_fn(data, f, get_impl, None):
            # filter doesn't match, continue iteration
            return True

    if state.select:
        data = state.select_fn([data], state.select)[0]

    if state.count_only:
        state.count += 1
    else:
        state.results.append(data)

    if state.single_result:
        # halt iterator
        return False

    if state.recursive:
        getattr(hdl, state.iterator_fn_name)(
            callback=generic_query_callback, state=state
        )

    return True


def generic_query(
    rsrc_iterator: callable,
    filters_in: list,
    options_in: dict,
    zfs_options: dict,
):
    # parse query-options
    options, select, order_by = generic_filters.validate_options(options_in)

    # set up callback state
    state = QueryFiltersCallbackState(
        iterator_fn_name=rsrc_iterator.__name__,
        filters=filters_in,
        filter_fn=generic_filters.eval_filter,
        get_fn=get_impl,
        select_fn=generic_filters.do_select,
        select=select,
        single_result=options["get"] and not order_by,
        count_only=options["count"],
        recursive=zfs_options.pop("recursive", False),
        propset=zfs_options.pop("propset", None),
        results=[],
    )

    # do iteration
    rsrc_iterator(callback=generic_query_callback, state=state, **zfs_options)

    # optimization where request is only for single result with no pagination
    if state.single_result:
        if not state.results:
            raise MatchNotFound()

        return state.results[0]

    if options["count"]:
        return state.count

    results = generic_filters.do_order(state.results, order_by)

    if options["get"]:
        if not state.results:
            raise MatchNotFound()

        return state.results[0]

    if offset := options.get("offset", 0):
        results = results[offset:]

    if limit := options.get("limit", 0):
        results = results[:limit]

    return results


lz = open_handle()
rsrc = lz.open_resource(name="dozer")
print("RECURSIVE FILESYSTEM:")
pprint(
    generic_query(
        rsrc.iter_filesystems,
        [],
        {"get": False, "count": False, "select": []},
        {"recursive": True},
    )
)

print("\n\nNON-RECURSIVE FILESYSTEM:")
pprint(
    generic_query(
        rsrc.iter_filesystems,
        [["name", "=", "dozer/share"]],
        {"get": True, "count": False, "select": []},
        {},
    )
)

print("\n\nNON-RECURSIVE SNAPSHOT:")
pprint(
    generic_query(
        rsrc.iter_snapshots,
        [["name", "=", "dozer@foo"]],
        {"get": True, "count": False, "select": []},
        {"fast": True},
    )
)
