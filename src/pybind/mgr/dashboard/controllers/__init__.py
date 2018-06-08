# -*- coding: utf-8 -*-
# pylint: disable=protected-access,too-many-branches
from __future__ import absolute_import

import collections
import importlib
import inspect
import json
import os
import pkgutil
import sys

import cherrypy
from six import add_metaclass

from .. import logger
from ..settings import Settings
from ..tools import Session, wraps, getargspec, TaskManager
from ..exceptions import ViewCacheNoDataException, DashboardException
from ..services.exception import serialize_dashboard_exception


class Controller(object):
    def __init__(self, path, base_url=None):
        self.path = path
        self.base_url = base_url

        if self.path and self.path[0] != "/":
            self.path = "/" + self.path

        if self.base_url is None:
            self.base_url = ""
        elif self.base_url == "/":
            self.base_url = ""

        if self.base_url == "" and self.path == "":
            self.base_url = "/"

    def __call__(self, cls):
        cls._cp_controller_ = True
        cls._cp_path_ = "{}{}".format(self.base_url, self.path)

        config = {
            'tools.sessions.on': True,
            'tools.sessions.name': Session.NAME,
            'tools.session_expire_at_browser_close.on': True,
            'tools.dashboard_exception_handler.on': True
        }
        if not hasattr(cls, '_cp_config'):
            cls._cp_config = {}
        if 'tools.authenticate.on' not in cls._cp_config:
            config['tools.authenticate.on'] = False
        cls._cp_config.update(config)
        return cls


class ApiController(Controller):
    def __init__(self, path):
        super(ApiController, self).__init__(path, base_url="/api")

    def __call__(self, cls):
        cls = super(ApiController, self).__call__(cls)
        cls._api_endpoint = True
        return cls


class UiApiController(Controller):
    def __init__(self, path):
        super(UiApiController, self).__init__(path, base_url="/ui-api")


def AuthRequired(enabled=True):
    def decorate(cls):
        if not hasattr(cls, '_cp_config'):
            cls._cp_config = {
                'tools.authenticate.on': enabled
            }
        else:
            cls._cp_config['tools.authenticate.on'] = enabled
        return cls
    return decorate


def Endpoint(method=None, path=None, path_params=None, query_params=None,
             json_response=True, proxy=False):

    if method is None:
        method = 'GET'
    elif not isinstance(method, str) or \
            method.upper() not in ['GET', 'POST', 'DELETE', 'PUT']:
        raise TypeError("Possible values for method are: 'GET', 'POST', "
                        "'DELETE', or 'PUT'")

    method = method.upper()

    if method in ['GET', 'DELETE']:
        if path_params is not None:
            raise TypeError("path_params should not be used for {} "
                            "endpoints. All function params are considered"
                            " path parameters by default".format(method))

    if path_params is None:
        if method in ['POST', 'PUT']:
            path_params = []

    if query_params is None:
        query_params = []

    def _wrapper(func):
        if method in ['POST', 'PUT']:
            func_params = _get_function_params(func)
            for param in func_params:
                if param['name'] in path_params and not param['required']:
                    raise TypeError("path_params can only reference "
                                    "non-optional function parameters")

        if func.__name__ == '__call__' and path is None:
            e_path = ""
        else:
            e_path = path

        if e_path is not None:
            e_path = e_path.strip()
            if e_path and e_path[0] != "/":
                e_path = "/" + e_path
            elif e_path == "/":
                e_path = ""

        func._endpoint = {
            'method': method,
            'path': e_path,
            'path_params': path_params,
            'query_params': query_params,
            'json_response': json_response,
            'proxy': proxy
        }
        return func
    return _wrapper


def Proxy(path=None):
    if path is None:
        path = ""
    elif path == "/":
        path = ""
    path += "/{path:.*}"
    return Endpoint(path=path, proxy=True)


def load_controllers():
    # setting sys.path properly when not running under the mgr
    controllers_dir = os.path.dirname(os.path.realpath(__file__))
    dashboard_dir = os.path.dirname(controllers_dir)
    mgr_dir = os.path.dirname(dashboard_dir)
    logger.debug("LC: controllers_dir=%s", controllers_dir)
    logger.debug("LC: dashboard_dir=%s", dashboard_dir)
    logger.debug("LC: mgr_dir=%s", mgr_dir)
    if mgr_dir not in sys.path:
        sys.path.append(mgr_dir)

    controllers = []
    mods = [mod for _, mod, _ in pkgutil.iter_modules([controllers_dir])]
    logger.debug("LC: mods=%s", mods)
    for mod_name in mods:
        mod = importlib.import_module('.controllers.{}'.format(mod_name),
                                      package='dashboard')
        for _, cls in mod.__dict__.items():
            # Controllers MUST be derived from the class BaseController.
            if inspect.isclass(cls) and issubclass(cls, BaseController) and \
                    hasattr(cls, '_cp_controller_'):
                if cls._cp_path_.startswith(':'):
                    # invalid _cp_path_ value
                    logger.error("Invalid url prefix '%s' for controller '%s'",
                                 cls._cp_path_, cls.__name__)
                    continue
                controllers.append(cls)

    return controllers


ENDPOINT_MAP = collections.defaultdict(list)


def generate_controller_routes(ctrl_class, mapper, base_url):
    inst = ctrl_class()
    endp_base_urls = set()

    for endpoint in ctrl_class.endpoints():
        if endpoint.proxy:
            conditions = None
        else:
            conditions = dict(method=[endpoint.method])

        endp_url = endpoint.url
        if base_url == "/":
            base_url = ""
        if endp_url == "/" and base_url:
            endp_url = ""
        url = "{}{}".format(base_url, endp_url)

        if '/' in url[len(base_url)+1:]:
            endp_base_urls.add(url[:len(base_url)+1+endp_url[1:].find('/')])
        else:
            endp_base_urls.add(url)

        logger.debug("Mapped [%s] to %s:%s restricted to %s",
                     url, ctrl_class.__name__, endpoint.action,
                     endpoint.method)

        ENDPOINT_MAP[endpoint.url].append(endpoint)

        name = ctrl_class.__name__ + ":" + endpoint.action
        mapper.connect(name, url, controller=inst, action=endpoint.action,
                       conditions=conditions)

        # adding route with trailing slash
        name += "/"
        url += "/"
        mapper.connect(name, url, controller=inst, action=endpoint.action,
                       conditions=conditions)

    return endp_base_urls


def generate_routes(url_prefix):
    mapper = cherrypy.dispatch.RoutesDispatcher()
    ctrls = load_controllers()

    parent_urls = set()
    for ctrl in ctrls:
        parent_urls.update(generate_controller_routes(ctrl, mapper,
                                                      "{}".format(url_prefix)))

    logger.debug("list of parent paths: %s", parent_urls)
    return mapper, parent_urls


def json_error_page(status, message, traceback, version):
    cherrypy.response.headers['Content-Type'] = 'application/json'
    return json.dumps(dict(status=status, detail=message, traceback=traceback,
                           version=version))


class Task(object):
    def __init__(self, name, metadata, wait_for=5.0, exception_handler=None):
        self.name = name
        if isinstance(metadata, list):
            self.metadata = dict([(e[1:-1], e) for e in metadata])
        else:
            self.metadata = metadata
        self.wait_for = wait_for
        self.exception_handler = exception_handler

    def _gen_arg_map(self, func, args, kwargs):
        # pylint: disable=deprecated-method
        arg_map = {}
        if sys.version_info > (3, 0):  # pylint: disable=no-else-return
            sig = inspect.signature(func)
            arg_list = [a for a in sig.parameters]
        else:
            sig = getargspec(func)
            arg_list = [a for a in sig.args]

        for idx, arg in enumerate(arg_list):
            if idx < len(args):
                arg_map[arg] = args[idx]
            else:
                if arg in kwargs:
                    arg_map[arg] = kwargs[arg]
            if arg in arg_map:
                # This is not a type error. We are using the index here.
                arg_map[idx] = arg_map[arg]

        return arg_map

    def __call__(self, func):
        @wraps(func)
        def wrapper(*args, **kwargs):
            arg_map = self._gen_arg_map(func, args, kwargs)
            md = {}
            for k, v in self.metadata.items():
                if isinstance(v, str) and v and v[0] == '{' and v[-1] == '}':
                    param = v[1:-1]
                    try:
                        pos = int(param)
                        md[k] = arg_map[pos]
                    except ValueError:
                        md[k] = arg_map[v[1:-1]]
                else:
                    md[k] = v
            task = TaskManager.run(self.name, md, func, args, kwargs,
                                   exception_handler=self.exception_handler)
            try:
                status, value = task.wait(self.wait_for)
            except Exception as ex:
                if task.ret_value:
                    # exception was handled by task.exception_handler
                    if 'status' in task.ret_value:
                        status = task.ret_value['status']
                    else:
                        status = getattr(ex, 'status', 500)
                    cherrypy.response.status = status
                    return task.ret_value
                raise ex
            if status == TaskManager.VALUE_EXECUTING:
                cherrypy.response.status = 202
                return {'name': self.name, 'metadata': md}
            return value
        return wrapper


def _get_function_params(func):
    """
    Retrieves the list of parameters declared in function.
    Each parameter is represented as dict with keys:
      * name (str): the name of the parameter
      * required (bool): whether the parameter is required or not
      * default (obj): the parameter's default value
    """
    fspec = getargspec(func)

    func_params = []
    nd = len(fspec.args) if not fspec.defaults else -len(fspec.defaults)
    for param in fspec.args[1:nd]:
        func_params.append({'name': param, 'required': True})

    if fspec.defaults:
        for param, val in zip(fspec.args[nd:], fspec.defaults):
            func_params.append({
                'name': param,
                'required': False,
                'default': val
            })

    return func_params


class BaseController(object):
    """
    Base class for all controllers providing API endpoints.
    """

    class Endpoint(object):
        """
        An instance of this class represents an endpoint.
        """
        def __init__(self, ctrl, func):
            self.ctrl = ctrl
            self.func = func

            if not self.config['proxy']:
                setattr(self.ctrl, func.__name__, self.function)

        @property
        def config(self):
            func = self.func
            while not hasattr(func, '_endpoint'):
                if hasattr(func, "__wrapped__"):
                    func = func.__wrapped__
                else:
                    return None
            return func._endpoint

        @property
        def function(self):
            return self.ctrl._request_wrapper(self.func, self.method,
                                              self.config['json_response'])

        @property
        def method(self):
            return self.config['method']

        @property
        def proxy(self):
            return self.config['proxy']

        @property
        def url(self):
            if self.config['path'] is not None:
                url = "{}{}".format(self.ctrl.get_path(), self.config['path'])
            else:
                url = "{}/{}".format(self.ctrl.get_path(), self.func.__name__)

            ctrl_path_params = self.ctrl.get_path_param_names(
                self.config['path'])
            path_params = [p['name'] for p in self.path_params
                           if p['name'] not in ctrl_path_params]
            path_params = ["{{{}}}".format(p) for p in path_params]
            if path_params:
                url += "/{}".format("/".join(path_params))

            return url

        @property
        def action(self):
            return self.func.__name__

        @property
        def path_params(self):
            ctrl_path_params = self.ctrl.get_path_param_names(
                self.config['path'])
            func_params = _get_function_params(self.func)

            if self.method in ['GET', 'DELETE']:
                assert self.config['path_params'] is None

                return [p for p in func_params if p['name'] in ctrl_path_params
                        or (p['name'] not in self.config['query_params']
                            and p['required'])]

            # elif self.method in ['POST', 'PUT']:
            return [p for p in func_params if p['name'] in ctrl_path_params
                    or p['name'] in self.config['path_params']]

        @property
        def query_params(self):
            if self.method in ['GET', 'DELETE']:
                func_params = _get_function_params(self.func)
                path_params = [p['name'] for p in self.path_params]
                return [p for p in func_params if p['name'] not in path_params]

            # elif self.method in ['POST', 'PUT']:
            func_params = _get_function_params(self.func)
            return [p for p in func_params
                    if p['name'] in self.config['query_params']]

        @property
        def body_params(self):
            func_params = _get_function_params(self.func)
            path_params = [p['name'] for p in self.path_params]
            query_params = [p['name'] for p in self.query_params]
            return [p for p in func_params
                    if p['name'] not in path_params and
                    p['name'] not in query_params]

        @property
        def group(self):
            return self.ctrl.__name__

        @property
        def is_api(self):
            return hasattr(self.ctrl, '_api_endpoint')

        @property
        def is_secure(self):
            return self.ctrl._cp_config['tools.authenticate.on']

        def __repr__(self):
            return "Endpoint({}, {}, {})".format(self.url, self.method,
                                                 self.action)

    def __init__(self):
        logger.info('Initializing controller: %s -> %s',
                    self.__class__.__name__, self._cp_path_)

    @classmethod
    def get_path_param_names(cls, path_extension=None):
        if path_extension is None:
            path_extension = ""
        full_path = cls._cp_path_[1:] + path_extension
        path_params = []
        for step in full_path.split('/'):
            param = None
            if not step:
                continue
            if step[0] == ':':
                param = step[1:]
            elif step[0] == '{' and step[-1] == '}':
                param, _, _ = step[1:-1].partition(':')
            if param:
                path_params.append(param)
        return path_params

    @classmethod
    def get_path(cls):
        return cls._cp_path_

    @classmethod
    def endpoints(cls):
        """
        This method iterates over all the methods decorated with ``@endpoint``
        and creates an Endpoint object for each one of the methods.

        :return: A list of endpoint objects
        :rtype: list[BaseController.Endpoint]
        """
        result = []
        for _, func in inspect.getmembers(cls, predicate=callable):
            if hasattr(func, '_endpoint'):
                result.append(cls.Endpoint(cls, func))
        return result

    @staticmethod
    def _request_wrapper(func, method, json_response):
        @wraps(func)
        def inner(*args, **kwargs):
            if method in ['GET', 'DELETE']:
                ret = func(*args, **kwargs)

            elif cherrypy.request.headers.get('Content-Type', '') == \
                    'application/x-www-form-urlencoded':
                ret = func(*args, **kwargs)

            else:
                content_length = int(cherrypy.request.headers['Content-Length'])
                body = cherrypy.request.body.read(content_length)
                if not body:
                    return func(*args, **kwargs)

                try:
                    data = json.loads(body.decode('utf-8'))
                except Exception as e:
                    raise cherrypy.HTTPError(400, 'Failed to decode JSON: {}'
                                             .format(str(e)))
                kwargs.update(data.items())
                ret = func(*args, **kwargs)

            if json_response:
                cherrypy.response.headers['Content-Type'] = 'application/json'
                return json.dumps(ret).encode('utf8')
            return ret
        return inner


class RESTController(BaseController):
    """
    Base class for providing a RESTful interface to a resource.

    To use this class, simply derive a class from it and implement the methods
    you want to support.  The list of possible methods are:

    * list()
    * bulk_set(data)
    * create(data)
    * bulk_delete()
    * get(key)
    * set(data, key)
    * delete(key)

    Test with curl:

    curl -H "Content-Type: application/json" -X POST \
         -d '{"username":"xyz","password":"xyz"}'  https://127.0.0.1:8443/foo
    curl https://127.0.0.1:8443/foo
    curl https://127.0.0.1:8443/foo/0

    """

    # resource id parameter for using in get, set, and delete methods
    # should be overriden by subclasses.
    # to specify a composite id (two parameters) use '/'. e.g., "param1/param2".
    # If subclasses don't override this property we try to infer the structure
    # of the resourse ID.
    RESOURCE_ID = None

    _method_mapping = collections.OrderedDict([
        ('list', {'method': 'GET', 'resource': False, 'status': 200}),
        ('create', {'method': 'POST', 'resource': False, 'status': 201}),
        ('bulk_set', {'method': 'PUT', 'resource': False, 'status': 200}),
        ('bulk_delete', {'method': 'DELETE', 'resource': False, 'status': 204}),
        ('get', {'method': 'GET', 'resource': True, 'status': 200}),
        ('delete', {'method': 'DELETE', 'resource': True, 'status': 204}),
        ('set', {'method': 'PUT', 'resource': True, 'status': 200})
    ])

    @classmethod
    def infer_resource_id(cls):
        if cls.RESOURCE_ID is not None:
            return cls.RESOURCE_ID.split('/')
        for k, v in cls._method_mapping.items():
            func = getattr(cls, k, None)
            while hasattr(func, "__wrapped__"):
                func = func.__wrapped__
            if v['resource'] and func:
                path_params = cls.get_path_param_names()
                params = _get_function_params(func)
                return [p['name'] for p in params
                        if p['required'] and p['name'] not in path_params]
        return None

    @classmethod
    def endpoints(cls):
        result = super(RESTController, cls).endpoints()
        for _, func in inspect.getmembers(cls, predicate=callable):
            no_resource_id_params = False
            status = 200
            method = None
            query_params = None
            path = ""

            if func.__name__ in cls._method_mapping:
                meth = cls._method_mapping[func.__name__]

                if meth['resource']:
                    res_id_params = cls.infer_resource_id()
                    if res_id_params is None:
                        no_resource_id_params = True
                    else:
                        res_id_params = ["{{{}}}".format(p) for p in res_id_params]
                        path += "/{}".format("/".join(res_id_params))

                status = meth['status']
                method = meth['method']

            elif hasattr(func, "_collection_method_"):
                path = "/{}".format(func.__name__)
                method = func._collection_method_['method']
                query_params = func._collection_method_['query_params']

            elif hasattr(func, "_resource_method_"):
                res_id_params = cls.infer_resource_id()
                if res_id_params is None:
                    no_resource_id_params = True
                else:
                    res_id_params = ["{{{}}}".format(p) for p in res_id_params]
                    path += "/{}".format("/".join(res_id_params))
                    path += "/{}".format(func.__name__)

                method = func._resource_method_['method']
                query_params = func._resource_method_['query_params']

            else:
                continue

            if no_resource_id_params:
                raise TypeError("Could not infer the resource ID parameters for"
                                " method {}. "
                                "Please specify the resource ID parameters "
                                "using the RESOURCE_ID class property"
                                .format(func.__name__))

            func = cls._status_code_wrapper(func, status)
            endp_func = Endpoint(method, path=path,
                                 query_params=query_params)(func)
            result.append(cls.Endpoint(cls, endp_func))

        return result

    @classmethod
    def _status_code_wrapper(cls, func, status_code):
        @wraps(func)
        def wrapper(*vpath, **params):
            cherrypy.response.status = status_code
            return func(*vpath, **params)

        return wrapper

    @staticmethod
    def Resource(method=None, query_params=None):
        if not method:
            method = 'GET'

        def _wrapper(func):
            func._resource_method_ = {
                'method': method,
                'query_params': query_params
            }
            return func
        return _wrapper

    @staticmethod
    def Collection(method=None, query_params=None):
        if not method:
            method = 'GET'

        def _wrapper(func):
            func._collection_method_ = {
                'method': method,
                'query_params': query_params
            }
            return func
        return _wrapper
