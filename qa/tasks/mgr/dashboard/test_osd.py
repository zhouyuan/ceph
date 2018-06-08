# -*- coding: utf-8 -*-

from __future__ import absolute_import

from .helper import DashboardTestCase, authenticate, JObj, JAny, JList, JLeaf, JTuple


class OsdTest(DashboardTestCase):

    def assert_in_and_not_none(self, data, properties):
        self.assertSchema(data, JObj({p: JAny(none=False) for p in properties}, allow_unknown=True))

    @authenticate
    def test_list(self):
        data = self._get('/api/osd')
        self.assertStatus(200)

        self.assertGreaterEqual(len(data), 1)
        data = data[0]
        self.assert_in_and_not_none(data, ['host', 'tree', 'state', 'stats', 'stats_history'])
        self.assert_in_and_not_none(data['host'], ['name'])
        self.assert_in_and_not_none(data['tree'], ['id'])
        self.assert_in_and_not_none(data['stats'], ['numpg', 'stat_bytes_used', 'stat_bytes',
                                                    'op_r', 'op_w'])
        self.assert_in_and_not_none(data['stats_history'], ['op_out_bytes', 'op_in_bytes'])
        self.assertSchema(data['stats_history']['op_out_bytes'],
                          JList(JTuple([JLeaf(int), JLeaf(float)])))

    @authenticate
    def test_details(self):
        data = self._get('/api/osd/0')
        self.assertStatus(200)
        self.assert_in_and_not_none(data, ['osd_metadata', 'histogram'])
        self.assert_in_and_not_none(data['histogram'], ['osd'])
        self.assert_in_and_not_none(data['histogram']['osd'], ['op_w_latency_in_bytes_histogram',
                                                               'op_r_latency_out_bytes_histogram'])

    @authenticate
    def test_scrub(self):
        self._post('/api/osd/0/scrub?deep=False')
        self.assertStatus(200)

        self._post('/api/osd/0/scrub?deep=True')
        self.assertStatus(200)
