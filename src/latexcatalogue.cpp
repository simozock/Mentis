#include "latexcatalogue.h"

#include <QCoreApplication>
#include <QStringList>

#include <iterator>

namespace LatexCatalogue {

namespace {

using C = Category;

// Ported from MentisNote's packages/editor-react/src/latexCatalogue.ts (curated set, FEAT-046 there);
// left out: entries that need display mode there (align, gather) and the commands
// JKQtMathText does not draw (\implies, \pmod, \sup, \inf, \boldsymbol, \overbrace, ...) or
// draws unlike LaTeX (\overset and \underset swap their arguments, \land and \lor show cap and cup).
const Entry kEntries[] = {
    // Greek
    {C::Greek, "\\alpha", QT_TRANSLATE_NOOP("LatexCatalogue", "alpha"), "", nullptr},
    {C::Greek, "\\beta", QT_TRANSLATE_NOOP("LatexCatalogue", "beta"), "", nullptr},
    {C::Greek, "\\gamma", QT_TRANSLATE_NOOP("LatexCatalogue", "gamma"), "", nullptr},
    {C::Greek, "\\Gamma", QT_TRANSLATE_NOOP("LatexCatalogue", "Gamma (capital)"), "maiuscola", nullptr},
    {C::Greek, "\\delta", QT_TRANSLATE_NOOP("LatexCatalogue", "delta"), "", nullptr},
    {C::Greek, "\\Delta", QT_TRANSLATE_NOOP("LatexCatalogue", "Delta (capital)"), "maiuscola variazione", nullptr},
    {C::Greek, "\\epsilon", QT_TRANSLATE_NOOP("LatexCatalogue", "epsilon"), "", nullptr},
    {C::Greek, "\\varepsilon", QT_TRANSLATE_NOOP("LatexCatalogue", "epsilon (variant)"), "", nullptr},
    {C::Greek, "\\zeta", QT_TRANSLATE_NOOP("LatexCatalogue", "zeta"), "", nullptr},
    {C::Greek, "\\eta", QT_TRANSLATE_NOOP("LatexCatalogue", "eta"), "", nullptr},
    {C::Greek, "\\theta", QT_TRANSLATE_NOOP("LatexCatalogue", "theta"), "angolo angle", nullptr},
    {C::Greek, "\\Theta", QT_TRANSLATE_NOOP("LatexCatalogue", "Theta (capital)"), "maiuscola", nullptr},
    {C::Greek, "\\vartheta", QT_TRANSLATE_NOOP("LatexCatalogue", "theta (variant)"), "", nullptr},
    {C::Greek, "\\iota", QT_TRANSLATE_NOOP("LatexCatalogue", "iota"), "", nullptr},
    {C::Greek, "\\kappa", QT_TRANSLATE_NOOP("LatexCatalogue", "kappa"), "", nullptr},
    {C::Greek, "\\lambda", QT_TRANSLATE_NOOP("LatexCatalogue", "lambda"), "autovalore eigenvalue", nullptr},
    {C::Greek, "\\Lambda", QT_TRANSLATE_NOOP("LatexCatalogue", "Lambda (capital)"), "maiuscola", nullptr},
    {C::Greek, "\\mu", QT_TRANSLATE_NOOP("LatexCatalogue", "mu"), "micro", nullptr},
    {C::Greek, "\\nu", QT_TRANSLATE_NOOP("LatexCatalogue", "nu"), "", nullptr},
    {C::Greek, "\\xi", QT_TRANSLATE_NOOP("LatexCatalogue", "xi"), "", nullptr},
    {C::Greek, "\\Xi", QT_TRANSLATE_NOOP("LatexCatalogue", "Xi (capital)"), "maiuscola", nullptr},
    {C::Greek, "\\pi", QT_TRANSLATE_NOOP("LatexCatalogue", "pi"), "pi greco", nullptr},
    {C::Greek, "\\Pi", QT_TRANSLATE_NOOP("LatexCatalogue", "Pi (capital)"), "maiuscola", nullptr},
    {C::Greek, "\\rho", QT_TRANSLATE_NOOP("LatexCatalogue", "rho"), "", nullptr},
    {C::Greek, "\\varrho", QT_TRANSLATE_NOOP("LatexCatalogue", "rho (variant)"), "", nullptr},
    {C::Greek, "\\sigma", QT_TRANSLATE_NOOP("LatexCatalogue", "sigma"), "", nullptr},
    {C::Greek, "\\Sigma", QT_TRANSLATE_NOOP("LatexCatalogue", "Sigma (capital)"), "maiuscola", nullptr},
    {C::Greek, "\\tau", QT_TRANSLATE_NOOP("LatexCatalogue", "tau"), "", nullptr},
    {C::Greek, "\\upsilon", QT_TRANSLATE_NOOP("LatexCatalogue", "upsilon"), "", nullptr},
    {C::Greek, "\\phi", QT_TRANSLATE_NOOP("LatexCatalogue", "phi"), "", nullptr},
    {C::Greek, "\\varphi", QT_TRANSLATE_NOOP("LatexCatalogue", "phi (variant)"), "", nullptr},
    {C::Greek, "\\Phi", QT_TRANSLATE_NOOP("LatexCatalogue", "Phi (capital)"), "maiuscola", nullptr},
    {C::Greek, "\\chi", QT_TRANSLATE_NOOP("LatexCatalogue", "chi"), "", nullptr},
    {C::Greek, "\\psi", QT_TRANSLATE_NOOP("LatexCatalogue", "psi"), "", nullptr},
    {C::Greek, "\\Psi", QT_TRANSLATE_NOOP("LatexCatalogue", "Psi (capital)"), "maiuscola", nullptr},
    {C::Greek, "\\omega", QT_TRANSLATE_NOOP("LatexCatalogue", "omega"), "", nullptr},
    {C::Greek, "\\Omega", QT_TRANSLATE_NOOP("LatexCatalogue", "Omega (capital)"), "maiuscola ohm", nullptr},
    // Operators
    {C::Operators, "+", QT_TRANSLATE_NOOP("LatexCatalogue", "plus"), "più somma", nullptr},
    {C::Operators, "-", QT_TRANSLATE_NOOP("LatexCatalogue", "minus"), "meno sottrazione", nullptr},
    {C::Operators, "\\pm", QT_TRANSLATE_NOOP("LatexCatalogue", "plus-minus"), "più meno", nullptr},
    {C::Operators, "\\mp", QT_TRANSLATE_NOOP("LatexCatalogue", "minus-plus"), "meno più", nullptr},
    {C::Operators, "\\times", QT_TRANSLATE_NOOP("LatexCatalogue", "times"), "per moltiplicazione multiply cross", nullptr},
    {C::Operators, "\\cdot", QT_TRANSLATE_NOOP("LatexCatalogue", "dot product"), "punto per moltiplicazione multiply", nullptr},
    {C::Operators, "\\div", QT_TRANSLATE_NOOP("LatexCatalogue", "division"), "diviso divide", nullptr},
    {C::Operators, "\\ast", QT_TRANSLATE_NOOP("LatexCatalogue", "asterisk"), "asterisco", nullptr},
    {C::Operators, "\\star", QT_TRANSLATE_NOOP("LatexCatalogue", "star"), "stella", nullptr},
    {C::Operators, "\\circ", QT_TRANSLATE_NOOP("LatexCatalogue", "composition"), "composizione cerchio circle", nullptr},
    {C::Operators, "\\bullet", QT_TRANSLATE_NOOP("LatexCatalogue", "bullet"), "punto pieno", nullptr},
    {C::Operators, "\\oplus", QT_TRANSLATE_NOOP("LatexCatalogue", "circled plus"), "xor somma diretta direct sum", nullptr},
    {C::Operators, "\\otimes", QT_TRANSLATE_NOOP("LatexCatalogue", "tensor product"), "prodotto tensoriale circled times", nullptr},
    {C::Operators, "\\odot", QT_TRANSLATE_NOOP("LatexCatalogue", "circled dot"), "", nullptr},
    {C::Operators, "\\wedge", QT_TRANSLATE_NOOP("LatexCatalogue", "wedge (and)"), "e logico and", nullptr},
    {C::Operators, "\\vee", QT_TRANSLATE_NOOP("LatexCatalogue", "vee (or)"), "o logico or", nullptr},
    {C::Operators, "\\setminus", QT_TRANSLATE_NOOP("LatexCatalogue", "set minus"), "differenza insiemi", nullptr},
    {C::Operators, "\\dagger", QT_TRANSLATE_NOOP("LatexCatalogue", "dagger"), "aggiunto", nullptr},
    {C::Operators, "\\nabla", QT_TRANSLATE_NOOP("LatexCatalogue", "nabla"), "gradiente gradient del", nullptr},
    {C::Operators, "\\partial", QT_TRANSLATE_NOOP("LatexCatalogue", "partial"), "derivata parziale derivative", nullptr},
    // Relations
    {C::Relations, "=", QT_TRANSLATE_NOOP("LatexCatalogue", "equals"), "uguale", nullptr},
    {C::Relations, "\\neq", QT_TRANSLATE_NOOP("LatexCatalogue", "not equal"), "diverso", nullptr},
    {C::Relations, "<", QT_TRANSLATE_NOOP("LatexCatalogue", "less than"), "minore", nullptr},
    {C::Relations, ">", QT_TRANSLATE_NOOP("LatexCatalogue", "greater than"), "maggiore", nullptr},
    {C::Relations, "\\leq", QT_TRANSLATE_NOOP("LatexCatalogue", "less or equal"), "minore uguale le", nullptr},
    {C::Relations, "\\geq", QT_TRANSLATE_NOOP("LatexCatalogue", "greater or equal"), "maggiore uguale ge", nullptr},
    {C::Relations, "\\ll", QT_TRANSLATE_NOOP("LatexCatalogue", "much less"), "molto minore", nullptr},
    {C::Relations, "\\gg", QT_TRANSLATE_NOOP("LatexCatalogue", "much greater"), "molto maggiore", nullptr},
    {C::Relations, "\\approx", QT_TRANSLATE_NOOP("LatexCatalogue", "approximately"), "circa approssimato", nullptr},
    {C::Relations, "\\sim", QT_TRANSLATE_NOOP("LatexCatalogue", "similar"), "simile tilde", nullptr},
    {C::Relations, "\\simeq", QT_TRANSLATE_NOOP("LatexCatalogue", "similar or equal"), "simile uguale", nullptr},
    {C::Relations, "\\cong", QT_TRANSLATE_NOOP("LatexCatalogue", "congruent"), "congruente isomorfo", nullptr},
    {C::Relations, "\\equiv", QT_TRANSLATE_NOOP("LatexCatalogue", "equivalent"), "equivalente congruo identico", nullptr},
    {C::Relations, "\\propto", QT_TRANSLATE_NOOP("LatexCatalogue", "proportional"), "proporzionale", nullptr},
    {C::Relations, "\\perp", QT_TRANSLATE_NOOP("LatexCatalogue", "perpendicular"), "perpendicolare ortogonale", nullptr},
    {C::Relations, "\\parallel", QT_TRANSLATE_NOOP("LatexCatalogue", "parallel"), "parallelo", nullptr},
    {C::Relations, "\\mid", QT_TRANSLATE_NOOP("LatexCatalogue", "divides"), "divide tale che", nullptr},
    {C::Relations, "\\prec", QT_TRANSLATE_NOOP("LatexCatalogue", "precedes"), "precede", nullptr},
    {C::Relations, "\\succ", QT_TRANSLATE_NOOP("LatexCatalogue", "succeeds"), "segue", nullptr},
    {C::Relations, "\\models", QT_TRANSLATE_NOOP("LatexCatalogue", "models"), "modella", nullptr},
    {C::Relations, "\\vdash", QT_TRANSLATE_NOOP("LatexCatalogue", "proves"), "deduce", nullptr},
    {C::Relations, "\\stackrel{}{=}", QT_TRANSLATE_NOOP("LatexCatalogue", "stacked over relation"), "sopra uguale", "\\stackrel{\\text{def}}{=}"},
    // Arrows
    {C::Arrows, "\\to", QT_TRANSLATE_NOOP("LatexCatalogue", "to"), "freccia tende arrow", nullptr},
    {C::Arrows, "\\rightarrow", QT_TRANSLATE_NOOP("LatexCatalogue", "right arrow"), "freccia destra", nullptr},
    {C::Arrows, "\\leftarrow", QT_TRANSLATE_NOOP("LatexCatalogue", "left arrow"), "freccia sinistra gets", nullptr},
    {C::Arrows, "\\leftrightarrow", QT_TRANSLATE_NOOP("LatexCatalogue", "left-right arrow"), "freccia doppia", nullptr},
    {C::Arrows, "\\Rightarrow", QT_TRANSLATE_NOOP("LatexCatalogue", "implies"), "implica freccia doppia", nullptr},
    {C::Arrows, "\\Leftarrow", QT_TRANSLATE_NOOP("LatexCatalogue", "is implied by"), "implicato freccia", nullptr},
    {C::Arrows, "\\Leftrightarrow", QT_TRANSLATE_NOOP("LatexCatalogue", "if and only if"), "se e solo se iff freccia", nullptr},
    {C::Arrows, "\\iff", QT_TRANSLATE_NOOP("LatexCatalogue", "iff"), "se e solo se", nullptr},
    {C::Arrows, "\\mapsto", QT_TRANSLATE_NOOP("LatexCatalogue", "maps to"), "mappa freccia", nullptr},
    {C::Arrows, "\\longrightarrow", QT_TRANSLATE_NOOP("LatexCatalogue", "long right arrow"), "freccia lunga", nullptr},
    {C::Arrows, "\\uparrow", QT_TRANSLATE_NOOP("LatexCatalogue", "up arrow"), "freccia su", nullptr},
    {C::Arrows, "\\downarrow", QT_TRANSLATE_NOOP("LatexCatalogue", "down arrow"), "freccia giù", nullptr},
    {C::Arrows, "\\updownarrow", QT_TRANSLATE_NOOP("LatexCatalogue", "up-down arrow"), "freccia", nullptr},
    {C::Arrows, "\\nearrow", QT_TRANSLATE_NOOP("LatexCatalogue", "north-east arrow"), "freccia diagonale", nullptr},
    {C::Arrows, "\\searrow", QT_TRANSLATE_NOOP("LatexCatalogue", "south-east arrow"), "freccia diagonale", nullptr},
    // Structures
    {C::Structures, "\\frac{}{}", QT_TRANSLATE_NOOP("LatexCatalogue", "fraction"), "frazione divisione", "\\frac{a}{b}"},
    {C::Structures, "\\dfrac{}{}", QT_TRANSLATE_NOOP("LatexCatalogue", "fraction (display)"), "frazione grande", "\\dfrac{a}{b}"},
    {C::Structures, "\\tfrac{}{}", QT_TRANSLATE_NOOP("LatexCatalogue", "fraction (small)"), "frazione piccola", "\\tfrac{a}{b}"},
    {C::Structures, "\\cfrac{}{}", QT_TRANSLATE_NOOP("LatexCatalogue", "continued fraction"), "frazione continua", "\\cfrac{1}{1+x}"},
    {C::Structures, "\\binom{}{}", QT_TRANSLATE_NOOP("LatexCatalogue", "binomial"), "binomiale coefficiente", "\\binom{n}{k}"},
    {C::Structures, "\\sqrt{}", QT_TRANSLATE_NOOP("LatexCatalogue", "square root"), "radice quadrata", "\\sqrt{x}"},
    {C::Structures, "\\sqrt[]{}", QT_TRANSLATE_NOOP("LatexCatalogue", "n-th root"), "radice ennesima", "\\sqrt[n]{x}"},
    {C::Structures, "^{}", QT_TRANSLATE_NOOP("LatexCatalogue", "superscript"), "apice esponente potenza power", "x^{2}"},
    {C::Structures, "_{}", QT_TRANSLATE_NOOP("LatexCatalogue", "subscript"), "pedice indice", "x_{i}"},
    {C::Structures, "_{}^{}", QT_TRANSLATE_NOOP("LatexCatalogue", "sub- and superscript"), "pedice apice", "x_{i}^{2}"},
    {C::Structures, "{}'", QT_TRANSLATE_NOOP("LatexCatalogue", "prime"), "primo derivata", "f'"},
    {C::Structures, "\\overline{}", QT_TRANSLATE_NOOP("LatexCatalogue", "overline"), "barra sopra coniugato", "\\overline{z}"},
    {C::Structures, "\\underline{}", QT_TRANSLATE_NOOP("LatexCatalogue", "underline"), "sottolineato", "\\underline{x}"},
    {C::Structures, "\\boxed{}", QT_TRANSLATE_NOOP("LatexCatalogue", "box"), "riquadro scatola", "\\boxed{x=1}"},
    {C::Structures, "\\cancel{}", QT_TRANSLATE_NOOP("LatexCatalogue", "cancel"), "barrato semplifica", "\\cancel{x}"},
    // BigOperators
    {C::BigOperators, "\\sum_{}^{}", QT_TRANSLATE_NOOP("LatexCatalogue", "sum"), "sommatoria somma", "\\sum_{i=1}^{n}"},
    {C::BigOperators, "\\prod_{}^{}", QT_TRANSLATE_NOOP("LatexCatalogue", "product"), "produttoria prodotto", "\\prod_{i=1}^{n}"},
    {C::BigOperators, "\\int_{}^{}", QT_TRANSLATE_NOOP("LatexCatalogue", "integral"), "integrale definito", "\\int_{a}^{b}"},
    {C::BigOperators, "\\int", QT_TRANSLATE_NOOP("LatexCatalogue", "integral (plain)"), "integrale indefinito", nullptr},
    {C::BigOperators, "\\iint", QT_TRANSLATE_NOOP("LatexCatalogue", "double integral"), "integrale doppio", nullptr},
    {C::BigOperators, "\\iiint", QT_TRANSLATE_NOOP("LatexCatalogue", "triple integral"), "integrale triplo", nullptr},
    {C::BigOperators, "\\oint", QT_TRANSLATE_NOOP("LatexCatalogue", "contour integral"), "integrale curvilineo chiuso", nullptr},
    {C::BigOperators, "\\lim_{}", QT_TRANSLATE_NOOP("LatexCatalogue", "limit"), "limite tende", "\\lim_{x \\to 0}"},
    {C::BigOperators, "\\limsup_{}", QT_TRANSLATE_NOOP("LatexCatalogue", "limit superior"), "limite superiore", "\\limsup_{n}"},
    {C::BigOperators, "\\liminf_{}", QT_TRANSLATE_NOOP("LatexCatalogue", "limit inferior"), "limite inferiore", "\\liminf_{n}"},
    {C::BigOperators, "\\max_{}", QT_TRANSLATE_NOOP("LatexCatalogue", "maximum"), "massimo", "\\max_{x}"},
    {C::BigOperators, "\\min_{}", QT_TRANSLATE_NOOP("LatexCatalogue", "minimum"), "minimo", "\\min_{x}"},
    {C::BigOperators, "\\bigcup_{}", QT_TRANSLATE_NOOP("LatexCatalogue", "big union"), "unione", "\\bigcup_{i}"},
    {C::BigOperators, "\\bigcap_{}", QT_TRANSLATE_NOOP("LatexCatalogue", "big intersection"), "intersezione", "\\bigcap_{i}"},
    {C::BigOperators, "\\coprod", QT_TRANSLATE_NOOP("LatexCatalogue", "coproduct"), "coprodotto", nullptr},
    {C::BigOperators, "\\bigoplus", QT_TRANSLATE_NOOP("LatexCatalogue", "big direct sum"), "somma diretta", nullptr},
    // Accents
    {C::Accents, "\\hat{}", QT_TRANSLATE_NOOP("LatexCatalogue", "hat"), "cappello versore", "\\hat{x}"},
    {C::Accents, "\\widehat{}", QT_TRANSLATE_NOOP("LatexCatalogue", "wide hat"), "cappello largo", "\\widehat{xy}"},
    {C::Accents, "\\bar{}", QT_TRANSLATE_NOOP("LatexCatalogue", "bar"), "barra media", "\\bar{x}"},
    {C::Accents, "\\vec{}", QT_TRANSLATE_NOOP("LatexCatalogue", "vector"), "vettore freccia", "\\vec{v}"},
    {C::Accents, "\\overrightarrow{}", QT_TRANSLATE_NOOP("LatexCatalogue", "vector arrow (wide)"), "vettore freccia segmento", "\\overrightarrow{AB}"},
    {C::Accents, "\\dot{}", QT_TRANSLATE_NOOP("LatexCatalogue", "dot (derivative)"), "punto derivata", "\\dot{x}"},
    {C::Accents, "\\ddot{}", QT_TRANSLATE_NOOP("LatexCatalogue", "double dot"), "due punti derivata seconda", "\\ddot{x}"},
    {C::Accents, "\\tilde{}", QT_TRANSLATE_NOOP("LatexCatalogue", "tilde"), "tilde", "\\tilde{x}"},
    {C::Accents, "\\widetilde{}", QT_TRANSLATE_NOOP("LatexCatalogue", "wide tilde"), "tilde larga", "\\widetilde{xy}"},
    {C::Accents, "\\check{}", QT_TRANSLATE_NOOP("LatexCatalogue", "check"), "caron", "\\check{x}"},
    {C::Accents, "\\breve{}", QT_TRANSLATE_NOOP("LatexCatalogue", "breve"), "breve", "\\breve{x}"},
    {C::Accents, "\\acute{}", QT_TRANSLATE_NOOP("LatexCatalogue", "acute"), "acuto", "\\acute{x}"},
    {C::Accents, "\\grave{}", QT_TRANSLATE_NOOP("LatexCatalogue", "grave"), "grave", "\\grave{x}"},
    // Brackets
    {C::Brackets, "\\left( {} \\right)", QT_TRANSLATE_NOOP("LatexCatalogue", "parentheses (auto size)"), "parentesi tonde", "\\left( \\frac{a}{b} \\right)"},
    {C::Brackets, "\\left[ {} \\right]", QT_TRANSLATE_NOOP("LatexCatalogue", "brackets (auto size)"), "parentesi quadre", "\\left[ \\frac{a}{b} \\right]"},
    {C::Brackets, "\\left\\{ {} \\right\\}", QT_TRANSLATE_NOOP("LatexCatalogue", "braces (auto size)"), "parentesi graffe", "\\left\\{ \\frac{a}{b} \\right\\}"},
    {C::Brackets, "\\left| {} \\right|", QT_TRANSLATE_NOOP("LatexCatalogue", "absolute value"), "valore assoluto modulo", "\\left| x \\right|"},
    {C::Brackets, "\\left\\| {} \\right\\|", QT_TRANSLATE_NOOP("LatexCatalogue", "norm"), "norma", "\\left\\| v \\right\\|"},
    {C::Brackets, "\\langle {} \\rangle", QT_TRANSLATE_NOOP("LatexCatalogue", "angle brackets"), "parentesi angolari prodotto scalare", "\\langle u, v \\rangle"},
    {C::Brackets, "\\lfloor {} \\rfloor", QT_TRANSLATE_NOOP("LatexCatalogue", "floor"), "parte intera pavimento", "\\lfloor x \\rfloor"},
    {C::Brackets, "\\lceil {} \\rceil", QT_TRANSLATE_NOOP("LatexCatalogue", "ceiling"), "parte intera soffitto", "\\lceil x \\rceil"},
    {C::Brackets, "\\{ {} \\}", QT_TRANSLATE_NOOP("LatexCatalogue", "braces"), "graffe insieme", "\\{ a, b \\}"},
    {C::Brackets, "\\big(", QT_TRANSLATE_NOOP("LatexCatalogue", "bigger parenthesis"), "parentesi grande", nullptr},
    {C::Brackets, "\\Big(", QT_TRANSLATE_NOOP("LatexCatalogue", "big parenthesis"), "parentesi grande", nullptr},
    {C::Brackets, "\\left. {} \\right|_{}", QT_TRANSLATE_NOOP("LatexCatalogue", "evaluated at"), "valutato in", "\\left. f(x) \\right|_{x=0}"},
    // Matrices
    {C::Matrices, "\\begin{matrix} {} & {} \\\\ {} & {} \\end{matrix}", QT_TRANSLATE_NOOP("LatexCatalogue", "matrix"), "matrice", "\\begin{matrix} a & b \\\\ c & d \\end{matrix}"},
    {C::Matrices, "\\begin{pmatrix} {} & {} \\\\ {} & {} \\end{pmatrix}", QT_TRANSLATE_NOOP("LatexCatalogue", "matrix (parentheses)"), "matrice tonde", "\\begin{pmatrix} a & b \\\\ c & d \\end{pmatrix}"},
    {C::Matrices, "\\begin{bmatrix} {} & {} \\\\ {} & {} \\end{bmatrix}", QT_TRANSLATE_NOOP("LatexCatalogue", "matrix (brackets)"), "matrice quadre", "\\begin{bmatrix} a & b \\\\ c & d \\end{bmatrix}"},
    {C::Matrices, "\\begin{vmatrix} {} & {} \\\\ {} & {} \\end{vmatrix}", QT_TRANSLATE_NOOP("LatexCatalogue", "determinant"), "determinante matrice", "\\begin{vmatrix} a & b \\\\ c & d \\end{vmatrix}"},
    {C::Matrices, "\\begin{Bmatrix} {} & {} \\\\ {} & {} \\end{Bmatrix}", QT_TRANSLATE_NOOP("LatexCatalogue", "matrix (braces)"), "matrice graffe", "\\begin{Bmatrix} a & b \\\\ c & d \\end{Bmatrix}"},
    {C::Matrices, "\\begin{cases} {} & \\text{if } {} \\\\ {} & \\text{otherwise} \\end{cases}", QT_TRANSLATE_NOOP("LatexCatalogue", "cases"), "casi funzione a tratti piecewise sistema", "f(x)=\\begin{cases} 1 & x>0 \\\\ 0 & \\text{otherwise} \\end{cases}"},
    {C::Matrices, "\\begin{aligned} {} &= {} \\\\ &= {} \\end{aligned}", QT_TRANSLATE_NOOP("LatexCatalogue", "aligned equations"), "allineate equazioni sistema", "\\begin{aligned} a &= b \\\\ &= c \\end{aligned}"},
    {C::Matrices, "\\begin{array}{cc} {} & {} \\\\ {} & {} \\end{array}", QT_TRANSLATE_NOOP("LatexCatalogue", "array"), "tabella", "\\begin{array}{cc} a & b \\\\ c & d \\end{array}"},
    {C::Matrices, "\\cdots", QT_TRANSLATE_NOOP("LatexCatalogue", "centered dots"), "puntini ellipsis", nullptr},
    {C::Matrices, "\\ldots", QT_TRANSLATE_NOOP("LatexCatalogue", "low dots"), "puntini ellipsis", nullptr},
    {C::Matrices, "\\vdots", QT_TRANSLATE_NOOP("LatexCatalogue", "vertical dots"), "puntini verticali", nullptr},
    {C::Matrices, "\\ddots", QT_TRANSLATE_NOOP("LatexCatalogue", "diagonal dots"), "puntini diagonali", nullptr},
    // Functions
    {C::Functions, "\\sin", QT_TRANSLATE_NOOP("LatexCatalogue", "sine"), "seno trigonometria", nullptr},
    {C::Functions, "\\cos", QT_TRANSLATE_NOOP("LatexCatalogue", "cosine"), "coseno trigonometria", nullptr},
    {C::Functions, "\\tan", QT_TRANSLATE_NOOP("LatexCatalogue", "tangent"), "tangente trigonometria", nullptr},
    {C::Functions, "\\cot", QT_TRANSLATE_NOOP("LatexCatalogue", "cotangent"), "cotangente", nullptr},
    {C::Functions, "\\sec", QT_TRANSLATE_NOOP("LatexCatalogue", "secant"), "secante", nullptr},
    {C::Functions, "\\arcsin", QT_TRANSLATE_NOOP("LatexCatalogue", "arcsine"), "arcoseno", nullptr},
    {C::Functions, "\\arccos", QT_TRANSLATE_NOOP("LatexCatalogue", "arccosine"), "arcocoseno", nullptr},
    {C::Functions, "\\arctan", QT_TRANSLATE_NOOP("LatexCatalogue", "arctangent"), "arcotangente", nullptr},
    {C::Functions, "\\sinh", QT_TRANSLATE_NOOP("LatexCatalogue", "hyperbolic sine"), "seno iperbolico", nullptr},
    {C::Functions, "\\cosh", QT_TRANSLATE_NOOP("LatexCatalogue", "hyperbolic cosine"), "coseno iperbolico", nullptr},
    {C::Functions, "\\tanh", QT_TRANSLATE_NOOP("LatexCatalogue", "hyperbolic tangent"), "tangente iperbolica", nullptr},
    {C::Functions, "\\log", QT_TRANSLATE_NOOP("LatexCatalogue", "logarithm"), "logaritmo", nullptr},
    {C::Functions, "\\log_{}", QT_TRANSLATE_NOOP("LatexCatalogue", "logarithm base"), "logaritmo base", "\\log_{2}"},
    {C::Functions, "\\ln", QT_TRANSLATE_NOOP("LatexCatalogue", "natural logarithm"), "logaritmo naturale", nullptr},
    {C::Functions, "\\exp", QT_TRANSLATE_NOOP("LatexCatalogue", "exponential"), "esponenziale", nullptr},
    {C::Functions, "e^{}", QT_TRANSLATE_NOOP("LatexCatalogue", "e to the power"), "esponenziale nepero", "e^{x}"},
    {C::Functions, "\\det", QT_TRANSLATE_NOOP("LatexCatalogue", "determinant"), "determinante", nullptr},
    {C::Functions, "\\dim", QT_TRANSLATE_NOOP("LatexCatalogue", "dimension"), "dimensione", nullptr},
    {C::Functions, "\\ker", QT_TRANSLATE_NOOP("LatexCatalogue", "kernel"), "nucleo", nullptr},
    {C::Functions, "\\deg", QT_TRANSLATE_NOOP("LatexCatalogue", "degree"), "grado", nullptr},
    {C::Functions, "\\gcd", QT_TRANSLATE_NOOP("LatexCatalogue", "greatest common divisor"), "massimo comune divisore mcd", nullptr},
    {C::Functions, "\\arg", QT_TRANSLATE_NOOP("LatexCatalogue", "argument"), "argomento", nullptr},
    {C::Functions, "\\operatorname{}", QT_TRANSLATE_NOOP("LatexCatalogue", "custom operator"), "operatore personalizzato", "\\operatorname{rank}"},
    {C::Functions, "\\frac{d}{d{}}", QT_TRANSLATE_NOOP("LatexCatalogue", "derivative"), "derivata", "\\frac{d}{dx}"},
    {C::Functions, "\\frac{\\partial}{\\partial {}}", QT_TRANSLATE_NOOP("LatexCatalogue", "partial derivative"), "derivata parziale", "\\frac{\\partial}{\\partial x}"},
    {C::Functions, "\\,d{}", QT_TRANSLATE_NOOP("LatexCatalogue", "differential"), "differenziale", "\\int f(x)\\,dx"},
    // Sets
    {C::Sets, "\\in", QT_TRANSLATE_NOOP("LatexCatalogue", "element of"), "appartiene elemento", nullptr},
    {C::Sets, "\\notin", QT_TRANSLATE_NOOP("LatexCatalogue", "not element of"), "non appartiene", nullptr},
    {C::Sets, "\\ni", QT_TRANSLATE_NOOP("LatexCatalogue", "contains as member"), "contiene", nullptr},
    {C::Sets, "\\subset", QT_TRANSLATE_NOOP("LatexCatalogue", "subset"), "sottoinsieme incluso", nullptr},
    {C::Sets, "\\subseteq", QT_TRANSLATE_NOOP("LatexCatalogue", "subset or equal"), "sottoinsieme incluso uguale", nullptr},
    {C::Sets, "\\supset", QT_TRANSLATE_NOOP("LatexCatalogue", "superset"), "sovrainsieme", nullptr},
    {C::Sets, "\\supseteq", QT_TRANSLATE_NOOP("LatexCatalogue", "superset or equal"), "sovrainsieme", nullptr},
    {C::Sets, "\\cup", QT_TRANSLATE_NOOP("LatexCatalogue", "union"), "unione", nullptr},
    {C::Sets, "\\cap", QT_TRANSLATE_NOOP("LatexCatalogue", "intersection"), "intersezione", nullptr},
    {C::Sets, "\\emptyset", QT_TRANSLATE_NOOP("LatexCatalogue", "empty set"), "insieme vuoto", nullptr},
    {C::Sets, "\\varnothing", QT_TRANSLATE_NOOP("LatexCatalogue", "empty set (variant)"), "insieme vuoto", nullptr},
    {C::Sets, "\\mathbb{N}", QT_TRANSLATE_NOOP("LatexCatalogue", "natural numbers"), "naturali insieme", nullptr},
    {C::Sets, "\\mathbb{Z}", QT_TRANSLATE_NOOP("LatexCatalogue", "integers"), "interi relativi insieme", nullptr},
    {C::Sets, "\\mathbb{Q}", QT_TRANSLATE_NOOP("LatexCatalogue", "rationals"), "razionali insieme", nullptr},
    {C::Sets, "\\mathbb{R}", QT_TRANSLATE_NOOP("LatexCatalogue", "real numbers"), "reali insieme", nullptr},
    {C::Sets, "\\mathbb{C}", QT_TRANSLATE_NOOP("LatexCatalogue", "complex numbers"), "complessi insieme", nullptr},
    {C::Sets, "\\forall", QT_TRANSLATE_NOOP("LatexCatalogue", "for all"), "per ogni quantificatore", nullptr},
    {C::Sets, "\\exists", QT_TRANSLATE_NOOP("LatexCatalogue", "exists"), "esiste quantificatore", nullptr},
    {C::Sets, "\\nexists", QT_TRANSLATE_NOOP("LatexCatalogue", "does not exist"), "non esiste", nullptr},
    {C::Sets, "\\neg", QT_TRANSLATE_NOOP("LatexCatalogue", "not"), "negazione non logico", nullptr},
    {C::Sets, "\\top", QT_TRANSLATE_NOOP("LatexCatalogue", "true"), "vero", nullptr},
    {C::Sets, "\\bot", QT_TRANSLATE_NOOP("LatexCatalogue", "false"), "falso", nullptr},
    {C::Sets, "\\therefore", QT_TRANSLATE_NOOP("LatexCatalogue", "therefore"), "quindi", nullptr},
    {C::Sets, "\\because", QT_TRANSLATE_NOOP("LatexCatalogue", "because"), "perché poiché", nullptr},
    {C::Sets, "\\{ {} \\mid {} \\}", QT_TRANSLATE_NOOP("LatexCatalogue", "set builder"), "insieme tale che", "\\{ x \\mid x > 0 \\}"},
    // Fonts
    {C::Fonts, "\\mathbb{}", QT_TRANSLATE_NOOP("LatexCatalogue", "blackboard bold"), "grassetto lavagna insiemi", "\\mathbb{R}"},
    {C::Fonts, "\\mathbf{}", QT_TRANSLATE_NOOP("LatexCatalogue", "bold"), "grassetto", "\\mathbf{v}"},
    {C::Fonts, "\\mathit{}", QT_TRANSLATE_NOOP("LatexCatalogue", "italic"), "corsivo", "\\mathit{x}"},
    {C::Fonts, "\\mathrm{}", QT_TRANSLATE_NOOP("LatexCatalogue", "roman (upright)"), "dritto tondo", "\\mathrm{d}"},
    {C::Fonts, "\\mathcal{}", QT_TRANSLATE_NOOP("LatexCatalogue", "calligraphic"), "calligrafico", "\\mathcal{L}"},
    {C::Fonts, "\\mathscr{}", QT_TRANSLATE_NOOP("LatexCatalogue", "script"), "corsivo inglese", "\\mathscr{F}"},
    {C::Fonts, "\\mathfrak{}", QT_TRANSLATE_NOOP("LatexCatalogue", "fraktur"), "gotico", "\\mathfrak{g}"},
    {C::Fonts, "\\mathsf{}", QT_TRANSLATE_NOOP("LatexCatalogue", "sans serif"), "senza grazie", "\\mathsf{A}"},
    {C::Fonts, "\\mathtt{}", QT_TRANSLATE_NOOP("LatexCatalogue", "typewriter"), "monospazio", "\\mathtt{x}"},
    {C::Fonts, "\\text{}", QT_TRANSLATE_NOOP("LatexCatalogue", "text"), "testo parole", "\\text{if } x"},
    {C::Fonts, "\\color{}{}", QT_TRANSLATE_NOOP("LatexCatalogue", "colour"), "colore", "\\color{red}{x}"},
    {C::Fonts, "\\displaystyle", QT_TRANSLATE_NOOP("LatexCatalogue", "display style"), "stile grande", "\\displaystyle\\sum_{i=1}^{n} i"},
    {C::Fonts, "\\,", QT_TRANSLATE_NOOP("LatexCatalogue", "thin space"), "spazio sottile", "a\\,b"},
    {C::Fonts, "\\;", QT_TRANSLATE_NOOP("LatexCatalogue", "thick space"), "spazio", "a\\;b"},
    {C::Fonts, "\\quad", QT_TRANSLATE_NOOP("LatexCatalogue", "quad space"), "spazio largo", "a\\quad b"},
    {C::Fonts, "\\qquad", QT_TRANSLATE_NOOP("LatexCatalogue", "double quad space"), "spazio molto largo", "a\\qquad b"},
    {C::Fonts, "\\!", QT_TRANSLATE_NOOP("LatexCatalogue", "negative space"), "spazio negativo", "a\\!b"},
    // Symbols
    {C::Symbols, "\\infty", QT_TRANSLATE_NOOP("LatexCatalogue", "infinity"), "infinito", nullptr},
    {C::Symbols, "\\aleph", QT_TRANSLATE_NOOP("LatexCatalogue", "aleph"), "cardinalità", nullptr},
    {C::Symbols, "\\hbar", QT_TRANSLATE_NOOP("LatexCatalogue", "h-bar"), "costante di planck", nullptr},
    {C::Symbols, "\\ell", QT_TRANSLATE_NOOP("LatexCatalogue", "script l"), "elle", nullptr},
    {C::Symbols, "\\Re", QT_TRANSLATE_NOOP("LatexCatalogue", "real part"), "parte reale", nullptr},
    {C::Symbols, "\\Im", QT_TRANSLATE_NOOP("LatexCatalogue", "imaginary part"), "parte immaginaria", nullptr},
    {C::Symbols, "\\angle", QT_TRANSLATE_NOOP("LatexCatalogue", "angle"), "angolo", nullptr},
    {C::Symbols, "^\\circ", QT_TRANSLATE_NOOP("LatexCatalogue", "degree"), "gradi", "90^\\circ"},
    {C::Symbols, "\\triangle", QT_TRANSLATE_NOOP("LatexCatalogue", "triangle"), "triangolo", nullptr},
    {C::Symbols, "\\square", QT_TRANSLATE_NOOP("LatexCatalogue", "square"), "quadrato", nullptr},
    {C::Symbols, "\\checkmark", QT_TRANSLATE_NOOP("LatexCatalogue", "check mark"), "spunta", nullptr},
    {C::Symbols, "\\prime", QT_TRANSLATE_NOOP("LatexCatalogue", "prime"), "primo", nullptr},
    {C::Symbols, "\\%", QT_TRANSLATE_NOOP("LatexCatalogue", "percent"), "percentuale", nullptr},
    {C::Symbols, "\\$", QT_TRANSLATE_NOOP("LatexCatalogue", "dollar"), "dollaro", nullptr},
    {C::Symbols, "\\#", QT_TRANSLATE_NOOP("LatexCatalogue", "hash"), "cancelletto", nullptr},
    {C::Symbols, "\\&", QT_TRANSLATE_NOOP("LatexCatalogue", "ampersand"), "e commerciale", nullptr},
    {C::Symbols, "\\_", QT_TRANSLATE_NOOP("LatexCatalogue", "underscore"), "trattino basso", nullptr},
    {C::Symbols, "\\dots", QT_TRANSLATE_NOOP("LatexCatalogue", "dots"), "puntini", nullptr},
};

const char *const kCategoryNames[] = {
    QT_TRANSLATE_NOOP("LatexCatalogue", "Greek letters"), QT_TRANSLATE_NOOP("LatexCatalogue", "Operators"),
    QT_TRANSLATE_NOOP("LatexCatalogue", "Relations"),     QT_TRANSLATE_NOOP("LatexCatalogue", "Arrows"),
    QT_TRANSLATE_NOOP("LatexCatalogue", "Structures"),    QT_TRANSLATE_NOOP("LatexCatalogue", "Big operators"),
    QT_TRANSLATE_NOOP("LatexCatalogue", "Accents"),       QT_TRANSLATE_NOOP("LatexCatalogue", "Brackets"),
    QT_TRANSLATE_NOOP("LatexCatalogue", "Matrices"),      QT_TRANSLATE_NOOP("LatexCatalogue", "Functions"),
    QT_TRANSLATE_NOOP("LatexCatalogue", "Sets and logic"), QT_TRANSLATE_NOOP("LatexCatalogue", "Fonts and spaces"),
    QT_TRANSLATE_NOOP("LatexCatalogue", "Symbols"),
};

}  // namespace

const QVector<Entry> &entries() {
    static const QVector<Entry> list(std::begin(kEntries), std::end(kEntries));
    return list;
}

QVector<Category> categories() {
    QVector<Category> list;
    for (int i = 0; i <= int(Category::Symbols); ++i) list.append(Category(i));
    return list;
}

QString categoryName(Category category) { return QCoreApplication::translate("LatexCatalogue", kCategoryNames[int(category)]); }

QString name(const Entry &entry) { return QCoreApplication::translate("LatexCatalogue", entry.name); }

QString previewCode(const Entry &entry) {
    if (entry.preview) return QString::fromUtf8(entry.preview);
    return QString::fromUtf8(entry.code).replace(QStringLiteral("{}"), QStringLiteral("{\\square}"));
}

QString fold(const QString &text) {
    const QString decomposed = text.normalized(QString::NormalizationForm_D);
    QString folded;
    folded.reserve(decomposed.size());
    for (const QChar c : decomposed)
        if (c.category() != QChar::Mark_NonSpacing) folded.append(c.toLower());
    return folded;
}

QStringList searchTexts() {
    QStringList texts;
    texts.reserve(entries().size());
    for (const Entry &entry : entries())
        texts.append(fold(QString::fromUtf8(entry.name) + u' ' + name(entry) + u' ' + QString::fromUtf8(entry.code) + u' '
                          + QString::fromUtf8(entry.keywords)));
    return texts;
}

QVector<int> search(const QStringList &texts, const QString &query, Category category, bool allCategories) {
    const QStringList words = fold(query).split(u' ', Qt::SkipEmptyParts);
    QVector<int> found;
    for (int i = 0; i < entries().size(); ++i) {
        if (!allCategories && entries()[i].category != category) continue;
        bool all = true;
        for (const QString &word : words)
            if (!texts[i].contains(word)) { all = false; break; }
        if (all) found.append(i);
    }
    return found;
}

Insertion insert(const QString &value, int start, int end, const QString &code) {
    const QString selected = value.mid(start, end - start);
    const int slot = code.indexOf(QStringLiteral("{}"));
    const QString inserted = slot < 0 ? code : code.left(slot + 1) + selected + code.mid(slot + 1);
    return {value.left(start) + inserted + value.mid(end), start + (slot < 0 ? int(inserted.size()) : slot + 1 + int(selected.size()))};
}

}  // namespace LatexCatalogue
