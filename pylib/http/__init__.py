from enum import IntEnum

__all__ = ['HTTPStatus', 'HTTPMethod']

class HTTPStatus(IntEnum):
    def __new__(cls, value, phrase, description=''):
        obj = int.__new__(cls, value)
        obj._value_ = value
        obj.phrase = phrase
        obj.description = description
        return obj

    CONTINUE = 100, 'Continue', 'Request received, please continue'
    SWITCHING_PROTOCOLS = 101, 'Switching Protocols', 'Switching to new protocol; obey Upgrade header'
    OK = 200, 'OK', 'Request fulfilled, document follows'
    CREATED = 201, 'Created', 'Document created, URL follows'
    ACCEPTED = 202, 'Accepted', 'Request accepted, processing continues off-line'
    NO_CONTENT = 204, 'No Content', 'Request fulfilled, nothing follows'
    MOVED_PERMANENTLY = 301, 'Moved Permanently', 'Object moved permanently -- see URI list'
    FOUND = 302, 'Found', 'Object moved temporarily -- see URI list'
    SEE_OTHER = 303, 'See Other', 'Object moved -- see Method and URL list'
    NOT_MODIFIED = 304, 'Not Modified', 'Document has not changed since given time'
    TEMPORARY_REDIRECT = 307, 'Temporary Redirect', 'Object moved temporarily -- see URI list'
    PERMANENT_REDIRECT = 308, 'Permanent Redirect', 'Object moved permanently -- see URI list'
    BAD_REQUEST = 400, 'Bad Request', 'Bad request syntax or unsupported method'
    UNAUTHORIZED = 401, 'Unauthorized', 'No permission -- see authorization schemes'
    FORBIDDEN = 403, 'Forbidden', 'Request forbidden -- authorization will not help'
    NOT_FOUND = 404, 'Not Found', 'Nothing matches the given URI'
    METHOD_NOT_ALLOWED = 405, 'Method Not Allowed', 'Specified method is invalid for this resource'
    REQUEST_TIMEOUT = 408, 'Request Timeout', 'Request timed out; try again later'
    CONFLICT = 409, 'Conflict', 'Request conflict'
    GONE = 410, 'Gone', 'URI no longer exists and has been permanently removed'
    IM_A_TEAPOT = 418, "I'm a Teapot", 'Server refuses to brew coffee because it is a teapot.'
    UNPROCESSABLE_CONTENT = 422, 'Unprocessable Content', 'Server is unable to process the contained instructions'
    TOO_MANY_REQUESTS = 429, 'Too Many Requests', 'The user has sent too many requests in a given amount of time'
    INTERNAL_SERVER_ERROR = 500, 'Internal Server Error', 'Server got itself in trouble'
    NOT_IMPLEMENTED = 501, 'Not Implemented', 'Server does not support this operation'
    BAD_GATEWAY = 502, 'Bad Gateway', 'Invalid responses from another server/proxy'
    SERVICE_UNAVAILABLE = 503, 'Service Unavailable', 'The server cannot process the request due to a high load'
    GATEWAY_TIMEOUT = 504, 'Gateway Timeout', 'The gateway server did not receive a timely response'


class HTTPMethod:
    CONNECT = 'CONNECT'
    DELETE = 'DELETE'
    GET = 'GET'
    HEAD = 'HEAD'
    OPTIONS = 'OPTIONS'
    PATCH = 'PATCH'
    POST = 'POST'
    PUT = 'PUT'
    TRACE = 'TRACE'
