--TEST--
Test infFuture of Timeval : basic functionality
--FILE--
<?php
var_dump(Grpc\Timeval::infFuture());
?>
===DONE===
--EXPECTF--
%s
object(Grpc\Timeval)#1 (0) {
}
===DONE===
