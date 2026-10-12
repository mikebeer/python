import unittest, io, re

def clean(text):
    out = []
    skipping = False
    for line in text.split('\n'):
        if skipping:
            if line.startswith(' '):
                continue
            skipping = False
        if line == 'Traceback (most recent call last):':
            skipping = True
        out.append(re.sub(r'in \d+\.\d+s', 'in Xs', line))
    return '\n'.join(out)

class Calc(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.log = ['setUpClass']
    @classmethod
    def tearDownClass(cls):
        print('tearDownClass', cls.log)
    def setUp(self):
        self.log.append('setUp ' + self._testMethodName)
        self.addCleanup(self.log.append, 'cleanup ' + self._testMethodName)
    def test_a_basic(self):
        self.assertEqual(1 + 1, 2)
        self.assertNotEqual(1, 2)
        self.assertTrue(1)
        self.assertFalse(0)
        self.assertIs(None, None)
        self.assertIsNot(1, None)
        self.assertIsNone(None)
        self.assertIsNotNone(0)
        self.assertIn('a', 'abc')
        self.assertNotIn('z', 'abc')
        self.assertIsInstance(1, int)
        self.assertNotIsInstance('s', int)
        self.assertGreater(2, 1)
        self.assertGreaterEqual(2, 2)
        self.assertLess(1, 2)
        self.assertLessEqual(2, 2)
        self.assertAlmostEqual(1.00000001, 1.0)
        self.assertNotAlmostEqual(1.1, 1.0)
        self.assertAlmostEqual(1.1, 1.0, delta=0.2)
        self.assertRegex('hello world', r'w.rld')
        self.assertNotRegex('hello', r'z')
        self.assertCountEqual([1, 2, 2], [2, 1, 2])
        self.assertListEqual([1, 2], [1, 2])
        self.assertTupleEqual((1,), (1,))
        self.assertSetEqual({1, 2}, {2, 1})
        self.assertDictEqual({'a': 1}, {'a': 1})
        self.assertMultiLineEqual('a\nb', 'a\nb')
        self.assertSequenceEqual([1, 2], (1, 2), seq_type=None) if False else None
    def test_b_raises(self):
        with self.assertRaises(ValueError) as cm:
            int('x')
        self.assertIsInstance(cm.exception, ValueError)
        with self.assertRaisesRegex(KeyError, 'foo'):
            {}['foo']
        self.assertRaises(ZeroDivisionError, lambda: 1 / 0)
        self.assertRaises(TypeError, len, 5)
    def test_c_fail_messages(self):
        for fn in [
            lambda: self.assertEqual(1, 2),
            lambda: self.assertEqual('a', 'b', 'custom msg'),
            lambda: self.assertEqual([1, 2, 3], [1, 2, 4]),
            lambda: self.assertEqual({'a': 1, 'b': 2}, {'a': 1, 'b': 3}),
            lambda: self.assertEqual((1, 2), (1, 2, 3)),
            lambda: self.assertEqual({1, 2}, {2, 3}),
            lambda: self.assertEqual('a\nb\nc', 'a\nx\nc'),
            lambda: self.assertTrue(0),
            lambda: self.assertFalse(1),
            lambda: self.assertIn(1, [2]),
            lambda: self.assertNotIn(1, [1]),
            lambda: self.assertIs(1, 2),
            lambda: self.assertIsNone(5),
            lambda: self.assertIsNotNone(None),
            lambda: self.assertIsInstance(1, str),
            lambda: self.assertGreater(1, 2),
            lambda: self.assertLessEqual(3, 2),
            lambda: self.assertAlmostEqual(1.0, 1.5),
            lambda: self.assertNotAlmostEqual(1.0, 1.0),
            lambda: self.assertRegex('abc', 'z+'),
            lambda: self.assertNotRegex('abc', 'b'),
            lambda: self.assertCountEqual([1, 2], [1, 3]),
            lambda: self.assertRaises(ValueError, lambda: None),
            lambda: self.assertRaisesRegex(ValueError, 'zzz', int, 'x'),
            lambda: self.fail('explicit'),
            lambda: self.assertListEqual([1], [2]),
            lambda: self.assertDictEqual({}, {'a': 1}),
        ]:
            try:
                fn()
                print('NO FAILURE')
            except AssertionError as e:
                print('---'); print(e)
    def test_d_subtest(self):
        for i in range(3):
            with self.subTest(i=i):
                self.assertNotEqual(i, 1)
    @unittest.skipIf(True, 'always')
    def test_e_skipif(self):
        pass
    @unittest.skipUnless(False, 'never')
    def test_f_skipunless(self):
        pass
    @unittest.expectedFailure
    def test_g_expected(self):
        self.assertEqual(1, 2)
    @unittest.expectedFailure
    def test_h_unexpected(self):
        pass
    def test_i_skiptest(self):
        self.skipTest('dyn skip')

class Fixture(unittest.TestCase):
    def setUp(self):
        raise RuntimeError('setup broke')
    def test_x(self):
        pass

class Fn(unittest.TestCase):
    def test_1(self): pass
    def test_2(self): self.assertEqual(1, 1)

def run(suite, verbosity=1):
    s = io.StringIO()
    r = unittest.TextTestRunner(stream=s, verbosity=verbosity).run(suite)
    print(clean(s.getvalue()))
    print(r.testsRun, len(r.failures), len(r.errors), len(r.skipped), len(r.expectedFailures), len(r.unexpectedSuccesses), r.wasSuccessful())
    return r

ld = unittest.TestLoader()
print(ld.getTestCaseNames(Calc))
run(ld.loadTestsFromTestCase(Calc), 2)
run(ld.loadTestsFromTestCase(Fixture))
suite = unittest.TestSuite()
suite.addTest(Fn('test_1'))
suite.addTest(unittest.FunctionTestCase(lambda: None))
suite.addTests([Fn('test_2')])
print(suite.countTestCases())
run(suite)
run(unittest.TestSuite([ld.loadTestsFromTestCase(Fn), ld.loadTestsFromTestCase(Fn)]), 2)
res = unittest.TestResult()
ld.loadTestsFromTestCase(Fn).run(res)
print(res.testsRun, res.wasSuccessful())
print(unittest.TestCase.id(Fn('test_1')), str(Fn('test_1')), Fn('test_1').shortDescription())

@unittest.skip('class skipped')
class Skipped(unittest.TestCase):
    def test_a(self): pass

class Ctx(unittest.TestCase):
    def test_ctx(self):
        with self.assertRaises(ValueError):
            raise ValueError
        with self.subTest('x'):
            pass
    def test_nested_sub(self):
        with self.subTest(a=1):
            with self.subTest(b=2):
                self.assertTrue(False)

run(ld.loadTestsFromTestCase(Skipped), 2)
run(ld.loadTestsFromTestCase(Ctx), 2)
out = io.StringIO()
prog = unittest.main(argv=['prog', '-v', 'Fn'], module=__import__('__main__'), exit=False, testRunner=unittest.TextTestRunner(stream=out))
print(clean(out.getvalue()))
print(prog.result.wasSuccessful())
r = unittest.TextTestRunner(stream=io.StringIO()).run(ld.loadTestsFromNames(['Fn.test_1', 'Fn.test_2'], __import__('__main__')))
print(r.testsRun)
