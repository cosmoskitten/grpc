@rem Copyright 2016, Google Inc.
@rem All rights reserved.
@rem
@rem Redistribution and use in source and binary forms, with or without
@rem modification, are permitted provided that the following conditions are
@rem met:
@rem
@rem     * Redistributions of source code must retain the above copyright
@rem notice, this list of conditions and the following disclaimer.
@rem     * Redistributions in binary form must reproduce the above
@rem copyright notice, this list of conditions and the following disclaimer
@rem in the documentation and/or other materials provided with the
@rem distribution.
@rem     * Neither the name of Google Inc. nor the names of its
@rem contributors may be used to endorse or promote products derived from
@rem this software without specific prior written permission.
@rem
@rem THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
@rem "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
@rem LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
@rem A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
@rem OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
@rem SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
@rem LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
@rem DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
@rem THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
@rem (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
@rem OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

@rem Performs nuget restore step for C#.

setlocal

@rem enter repo root
cd /d %~dp0\..\..

@rem Location of nuget.exe
set NUGET=C:\nuget\nuget.exe

if exist %NUGET% (
  @rem Restore Grpc packages by packages since Nuget client 3.4.4 doesnt support restore
  @rem by solution
  @rem Moving into each directory to let the restores work with both nuget 3.4 and 2.8
  %NUGET% restore vsprojects/grpc_csharp_ext.sln || goto :error

  cd src/csharp/Grpc.Auth || goto :error
  %NUGET% restore -PackagesDirectory ../packages || goto :error
  cd ..

  cd src/csharp/Grpc.Core || goto :error
  %NUGET% restore -PackagesDirectory ../packages || goto :error
  cd ..

  cd src/csharp/Grpc.Core.Tests || goto :error
  %NUGET% restore -PackagesDirectory ../packages || goto :error
  cd ..

  cd src/csharp/Grpc.Examples.MathClient || goto :error
  %NUGET% restore -PackagesDirectory ../packages || goto :error
  cd ..

  cd src/csharp/Grpc.Examples.MathServer || goto :error
  %NUGET% restore -PackagesDirectory ../packages || goto :error
  cd ..

  cd src/csharp/Grpc.Examples || goto :error
  %NUGET% restore -PackagesDirectory ../packages || goto :error
  cd ..

  cd src/csharp/Grpc.HealthCheck.Tests || goto :error
  %NUGET% restore -PackagesDirectory ../packages || goto :error
  cd ..

  cd src/csharp/Grpc.HealthCheck || goto :error
  %NUGET% restore -PackagesDirectory ../packages || goto :error
  cd ..

  cd src/csharp/Grpc.IntegrationTesting.Client || goto :error
  %NUGET% restore -PackagesDirectory ../packages || goto :error
  cd ..

  cd src/csharp/Grpc.IntegrationTesting.QpsWorker || goto :error
  %NUGET% restore -PackagesDirectory ../packages || goto :error
  cd ..

  cd src/csharp/Grpc.IntegrationTesting.StressClient || goto :error
  %NUGET% restore -PackagesDirectory ../packages || goto :error
  cd ..

  cd src/csharp/Grpc.IntegrationTesting || goto :error
  %NUGET% restore -PackagesDirectory ../packages || goto :error
  cd ..
)

endlocal

goto :EOF

:error
echo Failed!
exit /b %errorlevel%
