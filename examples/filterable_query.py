import truenas_pylibzfs
from dataclasses import dataclass

from middlewared.utils import filters, get_impl
from middlewared.service_exception import MatchNotFound

generic_filters = filters()


@dataclass
class QueryFiltersCallbackState:
    # query-filters, query-options fields
    filters: list  # query-filters
    filter_fn: callable  # function to do filtering
    get_fn: callable  # function to get value from dict
    select_fn: callable  # function to select values
    select: list | None = None  # list of fields to select. None means all.
    single_result: bool = False  # return single result with no pagination
    count_only: bool = False  # only count entries

    # ZFS fields
    propset: set | None = None  # set of properties to retrieve

    # results will be one of following
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

    return True


def generic_query(rsrc_iterator, filters_in, options_in, zfs_options):
    # parse query-options
    options, select, order_by = generic_filters.validate_options(options_in)

    # set up callback state
    state = QueryFiltersCallbackState(
        filters=filters_in,
        filter_fn=generic_filters.eval_filter,
        get_fn=get_impl,
        select_fn=generic_filters.do_select,
        select=select,
        single_result=options['get'] and not order_by,
        count_only=options['count'],
        propset=zfs_options.pop('propset', None),
        results=[]
    )

    # do iteration
    rsrc_iterator(
        callback=generic_query_callback,
        state=state,
        **zfs_options
    )

    # optimization where request is only for single result with no pagination
    if state.single_result:
        if not state.results:
            raise MatchNotFound()

        return state.results[0]

    if options['count']:
        return state.count

    results = generic_filters.do_order(state.results, order_by)

    if options['get']:
        if not state.results:
            raise MatchNotFound()

        return state.results[0]

    if options['offset']:
        results = results[options['offset']:]

    if options['limit']:
        results = results[:options['limit']]

    return results


lz = truenas_pylibzfs.open_handle()
rsrc = lz.open_resource(name='dozer')
options = {'get': True, 'count': False, 'select': []}

print(generic_query(rsrc.iter_filesystems, [['name', '=', 'dozer/share']], options, {}))
print(generic_query(rsrc.iter_snapshots, [['name', '=', 'dozer@foo']], options, {'fast': True}))
