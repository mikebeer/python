"""scipy.constants (CODATA 2022 values, as in scipy 1.18)"""
import math

Avogadro = 6.02214076e+23
Boltzmann = 1.380649e-23
Btu = 1055.05585262
Btu_IT = 1055.05585262
Btu_th = 1054.3502644888888
G = 6.6743e-11
Julian_year = 31557600.0
N_A = 6.02214076e+23
Planck = 6.62607015e-34
R = 8.31446261815324
Rydberg = 10973731.568157
Stefan_Boltzmann = 5.6703744191844314e-08
Wien = 0.0028977719551851727
acre = 4046.8564223999992
alpha = 0.0072973525643
angstrom = 1e-10
arcmin = 0.0002908882086657216
arcminute = 0.0002908882086657216
arcsec = 4.84813681109536e-06
arcsecond = 4.84813681109536e-06
astronomical_unit = 149597870700.0
atm = 101325.0
atmosphere = 101325.0
atomic_mass = 1.66053906892e-27
atto = 1e-18
au = 149597870700.0
bar = 100000.0
barrel = 0.15898729492799998
bbl = 0.15898729492799998
blob = 175.12683524647636
c = 299792458.0
calorie = 4.184
calorie_IT = 4.1868
calorie_th = 4.184
carat = 0.0002
centi = 0.01
day = 86400.0
deci = 0.1
degree = 0.017453292519943295
degree_Fahrenheit = 0.5555555555555556
deka = 10.0
dyn = 1e-05
dyne = 1e-05
e = 1.602176634e-19
eV = 1.602176634e-19
electron_mass = 9.1093837139e-31
electron_volt = 1.602176634e-19
elementary_charge = 1.602176634e-19
epsilon_0 = 8.8541878188e-12
erg = 1e-07
exa = 1e+18
femto = 1e-15
fermi = 1e-15
fine_structure = 0.0072973525643
fluid_ounce = 2.9573529562499998e-05
fluid_ounce_US = 2.9573529562499998e-05
fluid_ounce_imp = 2.84130625e-05
foot = 0.30479999999999996
g = 9.80665
gallon = 0.0037854117839999997
gallon_US = 0.0037854117839999997
gallon_imp = 0.00454609
gas_constant = 8.31446261815324
giga = 1000000000.0
golden = 1.618033988749895
golden_ratio = 1.618033988749895
grain = 6.479891e-05
gram = 0.001
gravitational_constant = 6.6743e-11
h = 6.62607015e-34
hbar = 1.0545718176461565e-34
hectare = 10000.0
hecto = 100.0
horsepower = 745.6998715822701
hour = 3600.0
hp = 745.6998715822701
inch = 0.0254
k = 1.380649e-23
kgf = 9.80665
kilo = 1000.0
kilogram_force = 9.80665
kmh = 0.2777777777777778
knot = 0.5144444444444445
lb = 0.45359236999999997
lbf = 4.4482216152605
light_year = 9460730472580800.0
liter = 0.001
litre = 0.001
long_ton = 1016.0469088
m_e = 9.1093837139e-31
m_n = 1.67492750056e-27
m_p = 1.67262192595e-27
m_u = 1.66053906892e-27
mach = 340.5
mega = 1000000.0
metric_ton = 1000.0
micro = 1e-06
micron = 1e-06
mil = 2.5399999999999997e-05
mile = 1609.3439999999998
milli = 0.001
minute = 60.0
mmHg = 133.32236842105263
mph = 0.44703999999999994
mu_0 = 1.25663706127e-06
nano = 1e-09
nautical_mile = 1852.0
neutron_mass = 1.67492750056e-27
ounce = 0.028349523124999998
oz = 0.028349523124999998
parsec = 3.085677581491367e+16
peta = 1000000000000000.0
pi = 3.141592653589793
pico = 1e-12
point = 0.00035277777777777776
pound = 0.45359236999999997
pound_force = 4.4482216152605
proton_mass = 1.67262192595e-27
psi = 6894.757293168361
pt = 0.00035277777777777776
quecto = 1e-30
quetta = 1e+30
ronna = 1e+27
ronto = 1e-27
short_ton = 907.1847399999999
sigma = 5.6703744191844314e-08
slinch = 175.12683524647636
slug = 14.593902937206364
speed_of_light = 299792458.0
speed_of_sound = 340.5
stone = 6.3502931799999995
survey_foot = 0.3048006096012192
survey_mile = 1609.3472186944373
tera = 1000000000000.0
ton_TNT = 4184000000.0
torr = 133.32236842105263
troy_ounce = 0.031103476799999998
troy_pound = 0.37324172159999996
u = 1.66053906892e-27
week = 604800.0
yard = 0.9143999999999999
year = 31536000.0
yocto = 1e-24
yotta = 1e+24
zepto = 1e-21
zero_Celsius = 273.15
zetta = 1e+21

physical_constants = {
    'speed of light in vacuum': (299792458.0, 'm s^-1', 0.0),
    'Planck constant': (6.62607015e-34, 'J Hz^-1', 0.0),
    'Boltzmann constant': (1.380649e-23, 'J K^-1', 0.0),
    'elementary charge': (1.602176634e-19, 'C', 0.0),
    'electron mass': (9.1093837139e-31, 'kg', 2.8e-40),
    'proton mass': (1.67262192595e-27, 'kg', 5.2e-37),
    'neutron mass': (1.67492750056e-27, 'kg', 8.5e-37),
    'atomic mass constant': (1.66053906892e-27, 'kg', 5.2e-37),
    'Avogadro constant': (6.02214076e+23, 'mol^-1', 0.0),
    'molar gas constant': (8.31446261815324, 'J mol^-1 K^-1', 0.0),
    'Newtonian constant of gravitation': (6.6743e-11, 'm^3 kg^-1 s^-2', 1.5e-15),
    'standard acceleration of gravity': (9.80665, 'm s^-2', 0.0),
    'fine-structure constant': (0.0072973525643, '', 1.1e-12),
    'Rydberg constant': (10973731.568157, 'm^-1', 1.2e-05),
    'Stefan-Boltzmann constant': (5.6703744191844314e-08, 'W m^-2 K^-4', 0.0),
    'Wien wavelength displacement law constant': (0.0028977719551851727, 'm K', 0.0),
    'vacuum electric permittivity': (8.8541878188e-12, 'F m^-1', 1.4e-21),
    'vacuum mag. permeability': (1.25663706127e-06, 'N A^-2', 2e-16),
    'Faraday constant': (96485.33212331001, 'C mol^-1', 0.0),
    'Bohr radius': (5.29177210544e-11, 'm', 8.2e-21),
    'electron volt': (1.602176634e-19, 'J', 0.0),
    'reduced Planck constant': (1.0545718176461565e-34, 'J s', 0.0),
    'Bohr magneton': (9.2740100657e-24, 'J T^-1', 2.9e-33),
    'nuclear magneton': (5.0507837393e-27, 'J T^-1', 1.6e-36),
    'atomic unit of length': (5.29177210544e-11, 'm', 8.2e-21),
    'standard atmosphere': (101325.0, 'Pa', 0.0),
    'Josephson constant': (483597848416983.6, 'Hz V^-1', 0.0),
    'von Klitzing constant': (25812.807459304513, 'ohm', 0.0),
    'Compton wavelength': (2.42631023538e-12, 'm', 7.6e-22),
    'classical electron radius': (2.8179403205e-15, 'm', 1.3e-24),
    'muon mass': (1.883531627e-28, 'kg', 4.2e-36),
    'tau mass': (3.16754e-27, 'kg', 2.1e-31),
    'deuteron mass': (3.3435837768e-27, 'kg', 1e-36),
    'alpha particle mass': (6.644657345e-27, 'kg', 2.1e-36),
    'Loschmidt constant (273.15 K, 101.325 kPa)': (2.686780111798444e+25, 'm^-3', 0.0),
    'molar volume of ideal gas (273.15 K, 101.325 kPa)': (0.022413969545014137, 'm^3 mol^-1', 0.0),
    'Planck mass': (2.176434e-08, 'kg', 2.4e-13),
    'Planck length': (1.616255e-35, 'm', 1.8e-40),
    'Planck time': (5.391247e-44, 's', 6e-49),
    'Planck temperature': (1.416784e+32, 'K', 1.6e+27),
}

kibi = 1024
mebi = 1024 ** 2
gibi = 1024 ** 3
tebi = 1024 ** 4
pebi = 1024 ** 5
exbi = 1024 ** 6
zebi = 1024 ** 7
yobi = 1024 ** 8


def value(key):
    return physical_constants[key][0]


def unit(key):
    return physical_constants[key][1]


def precision(key):
    return physical_constants[key][2] / physical_constants[key][0]


def find(sub=None, disp=False):
    if sub is None:
        result = sorted(physical_constants.keys())
    else:
        result = sorted([k for k in physical_constants if sub.lower() in k.lower()])
    if disp:
        for k in result:
            print(k)
        return None
    return result


def convert_temperature(val, old_scale, new_scale):
    import numpy as np
    k = {"celsius": "c", "kelvin": "k", "fahrenheit": "f", "rankine": "r"}
    o = old_scale.lower()
    n = new_scale.lower()
    if o in ("celsius", "c"):
        kel = val + zero_Celsius
    elif o in ("kelvin", "k"):
        kel = val
    elif o in ("fahrenheit", "f"):
        kel = (val - 32.0) * 5.0 / 9.0 + zero_Celsius
    elif o in ("rankine", "r"):
        kel = val * 5.0 / 9.0
    else:
        raise NotImplementedError("%s scale is unsupported: supported scales are Celsius, Kelvin, Fahrenheit, and Rankine" % old_scale)
    if n in ("celsius", "c"):
        return kel - zero_Celsius
    if n in ("kelvin", "k"):
        return kel
    if n in ("fahrenheit", "f"):
        return (kel - zero_Celsius) * 9.0 / 5.0 + 32.0
    if n in ("rankine", "r"):
        return kel * 9.0 / 5.0
    raise NotImplementedError("%s scale is unsupported: supported scales are Celsius, Kelvin, Fahrenheit, and Rankine" % new_scale)


def lambda2nu(lambda_):
    return c / lambda_


def nu2lambda(nu):
    return c / nu
