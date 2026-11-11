// Closed synthetic build metadata only; no native/device/source/configuration or filesystem access.
#include "product/diagnostic_report.h"
#include "product/json_dom.h"
#include "product_test_util.h"
int main(){using namespace om;DiagnosticBuild build;build.publicVersion="1.0.3";DiagnosticPreview preview;
    CHECK(!preview.valid());CHECK(makeDiagnosticPreview({},build,&preview)&&preview.valid());const auto excluded=preview.bytes();
    CHECK(excluded.find("\"build\":false")!=excluded.npos&&excluded.find("1.0.3")==excluded.npos&&preview.digest()==sha256Hex(excluded));
    CHECK(!makeDiagnosticPreview({},build,nullptr));
    for(auto platform:{DiagnosticPlatform::Windows,DiagnosticPlatform::Linux,DiagnosticPlatform::Other})
        for(auto architecture:{DiagnosticArchitecture::HostOnly,DiagnosticArchitecture::Sm120})
            for(auto compiler:{DiagnosticCompiler::Msvc,DiagnosticCompiler::Gcc,DiagnosticCompiler::Clang,DiagnosticCompiler::Unknown}){
                build.platform=platform;build.architecture=architecture;build.compiler=compiler;
                CHECK(makeDiagnosticPreview({true},build,&preview));const auto original=preview.bytes();
                CHECK(original.size()<1024&&original.back()=='\n'&&preview.digest()==sha256Hex(original));
                JsonValue json;std::string error;CHECK(parseJsonStrict(original,&json,&error)&&json.isObject());
                const auto *selected=json.find("build");CHECK(selected&&selected->isObject()&&selected->find("version")&&selected->find("version")->text=="1.0.3");
                CHECK(makeDiagnosticPreview({},build,&preview)&&preview.bytes()==excluded);
            }
    for(const auto &version:{"0.0.0","1.0.3","999.999.999","1.2.10"})CHECK(diagnosticPublicVersion(version));
    for(const auto &version:{"","1","1.2","1.2.3.4","01.2.3","1.2.03","1000.0.0","-1.2.3","1.2.3-fixture-private-label","fixture-private-label","1.2.3\n"}){
        CHECK(!diagnosticPublicVersion(version));build.publicVersion=version;
        CHECK(makeDiagnosticPreview({true},build,&preview)&&preview.bytes().find("\"version\":null")!=preview.bytes().npos);
        CHECK(preview.bytes().find("fixture-private-label")==preview.bytes().npos);
    }
    const auto before=preview.bytes(),digest=preview.digest();
    for(unsigned invalid:{2u,3u}){auto bad=build;bad.architecture=static_cast<DiagnosticArchitecture>(invalid);
        CHECK(!makeDiagnosticPreview({true},bad,&preview)&&preview.bytes()==before&&preview.digest()==digest);}
    {auto bad=build;bad.platform=static_cast<DiagnosticPlatform>(3);CHECK(!makeDiagnosticPreview({true},bad,&preview)&&preview.bytes()==before&&preview.digest()==digest);}
    for(unsigned invalid=4;invalid<256;++invalid){auto bad=build;bad.platform=static_cast<DiagnosticPlatform>(invalid);
        CHECK(!makeDiagnosticPreview({true},bad,&preview)&&preview.bytes()==before&&preview.digest()==digest);
        CHECK(makeDiagnosticPreview({},bad,&preview)&&preview.bytes()==excluded);CHECK(makeDiagnosticPreview({true},build,&preview));
        bad=build;bad.architecture=static_cast<DiagnosticArchitecture>(invalid);
        CHECK(!makeDiagnosticPreview({true},bad,&preview)&&preview.bytes()==before&&preview.digest()==digest);
        bad=build;bad.compiler=static_cast<DiagnosticCompiler>(invalid);
        CHECK(!makeDiagnosticPreview({true},bad,&preview)&&preview.bytes()==before&&preview.digest()==digest);
    }
    CHECK(sha256Hex("abc")=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    return finishTests("closed diagnostic build-only preview and exact-byte digest");
}
