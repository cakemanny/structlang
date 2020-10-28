from pystructlang import parse_and_convert


def main():
    fname = '../example.sl'
    # cause exception if not there
    with open(fname):
        pass
    program = parse_and_convert(fname)

    for decl in program:
        print(decl)
        print()


if __name__ == '__main__':
    main()
