import os, re

ROOT    = os.path.dirname(os.path.abspath(__file__))
PY_DIR  = os.path.join(ROOT, "py")
OUT_DIR = os.path.join(ROOT, "genhdr")
OUT     = os.path.join(OUT_DIR, "qstrdefs.generated.h")

if not os.path.isfile(os.path.join(PY_DIR, "runtime.c")):
    print("[ERROR] Khong tim thay py\\runtime.c")
    raise SystemExit(1)

os.makedirs(OUT_DIR, exist_ok=True)

BASE = [
    "", # Chuỗi rỗng -> Sinh ra MP_QSTR_
    "stdin","stdout","stderr","__main__",
    "_lt_stdin_gt_","_lt_stdout_gt_","_lt_stderr_gt_",
    "_lt_string_gt_","_lt_module_gt_","_lt_lambda_gt_",
    "_lt_genexpr_gt_","_lt_listcomp_gt_","_lt_dictcomp_gt_","_lt_setcomp_gt_",
    "__init__","__name__","__dict__","__class__","__module__",
    "__doc__","__file__","__path__","__qualname__","__import__","__build_class__",
    "print","object","type","int","str","bytes","bytearray","list","dict",
    "tuple","set","frozenset","bool","float","complex","range","enumerate",
    "zip","map","filter","reversed","sorted","sum","min","max","abs","len",
    "None","True","False","Ellipsis","NotImplemented",
    "Exception","BaseException","SystemExit","KeyboardInterrupt",
    "StopIteration","RuntimeError","ValueError","TypeError",
    "NameError","IndexError","KeyError","AttributeError",
    "NotImplementedError","OSError","ImportError","SyntaxError",
    "IndentationError","ZeroDivisionError","AssertionError","MemoryError",
    "OverflowError","ArithmeticError","LookupError","EOFError","UnicodeError",
    "self","args","kwargs",
    "vga_cls", "vga_color", "sys_info"
]

qstrs = set(BASE)

pat = re.compile(r'\bMP_QSTR_([A-Za-z0-9_]*)\b')
for dirpath, _, files in os.walk(PY_DIR):
    for fn in files:
        if fn.endswith((".c", ".h")):
            try:
                with open(os.path.join(dirpath, fn), "r", encoding="utf-8", errors="ignore") as f:
                    for m in pat.finditer(f.read()):
                        qstrs.add(m.group(1))
            except OSError:
                pass

def decode(name):
    repl = [("_lt_","<"),("_gt_",">"),("_eq_","="),("_amp_","&"),
            ("_hash_","#"),("_plus_","+"),("_minus_","-"),("_star_","*"),
            ("_slash_","/"),("_pipe_","|"),("_tilde_","~"),("_caret_","^"),
            ("_exclam_","!"),("_lparen_","("),("_rparen_",")"),
            ("_lbracket_","["),("_rbracket_","]"),("_lbrace_","{"),
            ("_rbrace_","}"),("_percent_","%"),("_at_","@"),
            ("_colon_",":"),("_semicolon_",";"),("_comma_",","),
            ("_dot_","."),("_quote_","'"),("_dquote_",'\"'),
            ("_backslash_","\\"),("_question_","?"),("_space_"," ")]
    s = name
    for a, b in repl:
        s = s.replace(a, b)
    return s

def qhash(s):
    h = 5381
    for c in s.encode("latin-1"):
        h = ((h * 33) ^ c) & 0xFFFFFFFF
    return h

with open(OUT, "w", encoding="utf-8") as f:
    # 1. Ghi hằng số MP_QSTRnull đặc biệt đầu tiên
    f.write('QDEF(MP_QSTRnull, 0x00000000, 0, "")\n')
    
    # 2. Ghi các hằng số QSTR tiêu chuẩn
    for name in sorted(qstrs):
        s = decode(name)
        ce = s.replace("\\","\\\\").replace('"','\\"')
        h = qhash(s)
        f.write('QDEF(MP_QSTR_{}, 0x{:08x}, {}, "{}")\n'.format(name, h, len(s), ce))

print("Da sinh {} qstr vao {}".format(len(qstrs) + 1, OUT))