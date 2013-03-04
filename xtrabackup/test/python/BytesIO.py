
# http://wiki.python.org/moin/BytesIO
#
# A skeleton one used for systems that don't have BytesIO.
#
# It's enough for subunit at least....

class BytesIO(object):
    """ A file-like API for reading and writing bytes objects.

    Mostly like StringIO, but write() calls modify the underlying
    bytes object.

    >>> b = bytes()
    >>> f = BytesIO(b, 'w')
    >>> f.write(bytes.fromhex('ca fe ba be'))
    >>> f.write(bytes.fromhex('57 41 56 45'))
    >>> b
    bytes([202, 254, 186, 190, 87, 65, 86, 69])
    """

    def __init__(self, buf, mode='r'):
        """ Create a new BytesIO for reading or writing the given buffer.

        buf - Back-end buffer for this BytesIO.  A bytes object.
            Actually, anything that supports len(), slice-assignment,
            and += will work.
        mode - One of 'r', 'w', 'a'.
            An optional 'b' is also allowed, but it doesn't do anything.
        """
        # XXX many 'mode' possibilities aren't allowed yet: 'rw+Ut'
        if len(mode) == 2 and mode[-1] == 'b':
            mode = mode[:-1]  # binary mode goes without saying
        if mode not in ('r', 'w', 'a'):
            raise ValueError("mode must be 'r', 'w', or 'a'")

        self._buf = buf
        self.mode = mode
        self.closed = False
        if self.mode == 'w':
            del buf[:]
            self._point = 0
        elif self.mode == 'r':
            self._point = 0
        else: # 'a'
            self._point = len(buf)

    def close(self):
        self.closed = True

    def _check_closed(self):
        if self.closed:
            raise ValueError("file is closed")

    def flush(self):
        self._check_closed()

    def next(self):
        line = self.readline()
        if len(line) == 0:
            raise StopIteration
        return line

    def read(self, size=None):
        self._check_closed()
        if size is None:
            e = len(self._buf)
        else:
            e = min(self._point + size, len(self._buf))
        r = self._buf[self._point:e]
        self._point = e
        return r

    def readline(self, size=None):
        self._check_closed()
        die  # XXX TODO - assume ascii and read a line

    def readlines(self, sizehint=None):
        # XXX TODO handle sizehint
        return list(self)

    def seek(self, offset, whence=0):
        self._check_closed()

        if whence == 0:
            self._point = offset
        elif whence == 1:
            self._point += offset
        elif whence == 2:
            self._point = len(self._buf) + offset
        else:
            raise ValueError("whence must be 0, 1, or 2")

        if self._point < 0:
            self._point = 0  # XXX is this right?

    def tell(self):
        self._check_closed()
        return self._point

    def truncate(self, size=None):
        self._check_closed()
        if size is None:
            size = self.tell()
        del self._buf[size:]

    def write(self, data):
        self._check_closed()
        amt = len(data)
        size = len(self._buf)
        if self.mode == 'a':
            self._point = size

        if self._point > size:
            if isinstance(b, bytes):
                blank = bytes([0])
            else:
                # Don't know what default value to insert, unfortunately
                raise ValueError("can't write past the end of this object")
            self._buf += blank * (self._point - size) + data
            self._point = len(self._buf)
        else:
            p = self._point
            self._buf[p:p + amt] = data
            self._point = min(p + amt, len(self._buf))

    def writelines(self, seq):
        for line in seq:
            self.write(line)

    def __iter__(self):
        return self

    @property
    def name(self):
        return repr(self)
