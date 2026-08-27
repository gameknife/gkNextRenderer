import os
import threading
import time

try:
    import renderdoc as rd
except Exception as error:
    result_path = os.environ["GK_RENDERDOC_RESULT_FILE"]
    with open(result_path, "w", encoding="utf-8") as result_file:
        result_file.write("ERROR: unable to import RenderDoc Python API: " + repr(error))
    raise


def write_result(value):
    result_path = os.environ["GK_RENDERDOC_RESULT_FILE"]
    with open(result_path, "w", encoding="utf-8") as result_file:
        result_file.write(value)


def capture():
    serial = os.environ["GK_RENDERDOC_SERIAL"]
    url = "adb://" + serial
    activity = "com.gknext.renderer/com.gknext.renderer.GkNextActivity"
    intent_args = "--ez gknext.renderdoc true"

    result, remote = rd.CreateRemoteServerConnection(url)
    if result.code != rd.ResultCode.Succeeded:
        protocol = rd.GetDeviceProtocolController("adb")
        if protocol is None:
            write_result("ERROR: RenderDoc adb device protocol is unavailable")
            return
        result = protocol.StartRemoteServer(url)
        if result.code != rd.ResultCode.Succeeded:
            write_result("ERROR: " + result.Message())
            return
        result, remote = rd.CreateRemoteServerConnection(url)
    if result.code != rd.ResultCode.Succeeded:
        write_result("ERROR: " + result.Message())
        return

    stop_ping = threading.Event()

    def ping_remote():
        while not stop_ping.wait(1.0):
            remote.Ping()

    ping_thread = threading.Thread(target=ping_remote)
    ping_thread.daemon = True
    ping_thread.start()
    target = None
    try:
        execute_result = remote.ExecuteAndInject(
            activity, "", intent_args, [], rd.CaptureOptions())
        if execute_result.result.code != rd.ResultCode.Succeeded:
            write_result("ERROR: " + execute_result.result.Message())
            return

        target = rd.CreateTargetControl(url, execute_result.ident, "gkNext-gnb", True)
        if target is None:
            write_result("ERROR: RenderDoc target control connection failed")
            return

        deadline = time.time() + 120.0
        while time.time() < deadline:
            message = target.ReceiveMessage(None)
            if message is not None and message.type == rd.TargetControlMessageType.NewCapture:
                write_result(message.newCapture.path)
                return
    except Exception as error:
        write_result("ERROR: " + str(error))
    finally:
        if target is not None:
            target.Shutdown()
        stop_ping.set()

    write_result("ERROR: timed out waiting for a RenderDoc capture")


capture()
raise SystemExit(0)
