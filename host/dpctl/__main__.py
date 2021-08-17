
import dpctl


try:
    exit(dpctl.main() or 0)
except Exception:# as e
    import traceback
    traceback.print_exc()
    exit(1)

