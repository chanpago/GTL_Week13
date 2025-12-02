"""
C++ 헤더 파일 파싱 모듈
UPROPERTY와 UFUNCTION 매크로를 찾아서 파싱합니다.
"""

import re
from pathlib import Path
from dataclasses import dataclass, field
from typing import List, Dict, Optional, Tuple
from macro_parser import MacroParser, create_macro_parser


class Property:
    """프로퍼티 정보"""
    # MacroParser 인스턴스 (클래스 변수 - dataclass 밖에서 선언)
    _macro_parser: Optional['MacroParser'] = None

    @classmethod
    def set_macro_parser(cls, parser: 'MacroParser'):
        """MacroParser 인스턴스 설정 (전역)"""
        cls._macro_parser = parser

    def __init__(
        self,
        name: str,
        type: str,
        category: str = "",
        editable: bool = True,
        tooltip: str = "",
        min_value: float = 0.0,
        max_value: float = 0.0,
        has_range: bool = False,
        metadata: Optional[Dict[str, str]] = None
    ):
        self.name = name
        self.type = type
        self.category = category
        self.editable = editable
        self.tooltip = tooltip
        self.min_value = min_value
        self.max_value = max_value
        self.has_range = has_range
        self.metadata = metadata or {}

    def get_property_type_macro(self) -> str:
        """타입에 맞는 ADD_PROPERTY 매크로 결정 (동적 감지)"""
        type_lower = self.type.lower()

        # ECombineMode 타입 체크 (물리 재질 결합 모드)
        if 'ecombinemode' in type_lower:
            return 'ADD_PROPERTY_COMBINE_MODE'

        # PhysMaterialPreset 체크 (변수명 기반)
        if 'physmaterialpreset' in self.name.lower():
            return 'ADD_PROPERTY_PHYS_MATERIAL_PRESET'

        # TArray 타입 체크 (포인터 체크보다 먼저)
        if 'tarray' in type_lower:
            # TArray<UMaterialInterface*> 같은 형태에서 내부 타입 추출
            match = re.search(r'tarray\s*<\s*(\w+)', self.type, re.IGNORECASE)
            if match:
                inner_type = match.group(1) + '*'  # 포인터 추가

                # MacroParser를 사용하여 동적으로 매크로 찾기
                if self._macro_parser:
                    macro_name = self._macro_parser.get_macro_for_type(inner_type)
                    if macro_name:
                        # 매크로 이름에서 EPropertyType 추출
                        property_type = self._macro_parser.get_property_type_enum(macro_name)
                        if property_type:
                            self.metadata['inner_type'] = property_type
                        else:
                            self.metadata['inner_type'] = 'EPropertyType::ObjectPtr'
                    else:
                        self.metadata['inner_type'] = 'EPropertyType::ObjectPtr'
                else:
                    # Fallback: 기존 하드코딩된 방식
                    inner_type_lower = inner_type.lower()
                    if 'umaterial' in inner_type_lower:
                        self.metadata['inner_type'] = 'EPropertyType::Material'
                    elif 'utexture' in inner_type_lower:
                        self.metadata['inner_type'] = 'EPropertyType::Texture'
                    elif 'usound' in inner_type_lower:
                        self.metadata['inner_type'] = 'EPropertyType::Sound'
                    elif 'ustaticmesh' in inner_type_lower:
                        self.metadata['inner_type'] = 'EPropertyType::StaticMesh'
                    elif 'uskeletalmesh' in inner_type_lower:
                        self.metadata['inner_type'] = 'EPropertyType::SkeletalMesh'
                    elif 'uparticlesystem' in inner_type_lower:
                        self.metadata['inner_type'] = 'EPropertyType::ParticleSystem'
                    else:
                        self.metadata['inner_type'] = 'EPropertyType::ObjectPtr'
            return 'ADD_PROPERTY_ARRAY'

        # 특수 타입 체크 (포인터보다 먼저)
        if 'ucurve' in type_lower or 'fcurve' in type_lower:
            # MacroParser에서 CURVE 매크로 찾기
            if self._macro_parser and 'ADD_PROPERTY_CURVE' in self._macro_parser.macros:
                return 'ADD_PROPERTY_CURVE'
            return 'ADD_PROPERTY_CURVE'

        # SRV (Shader Resource View) 타입 체크
        if 'srv' in type_lower or 'shaderresourceview' in type_lower:
            # MacroParser에서 SRV 매크로 찾기
            if self._macro_parser and 'ADD_PROPERTY_SRV' in self._macro_parser.macros:
                return 'ADD_PROPERTY_SRV'
            return 'ADD_PROPERTY_SRV'

        # 포인터 타입 체크 - MacroParser 사용 (자동 감지)
        if '*' in self.type:
            if self._macro_parser:
                macro_name = self._macro_parser.get_macro_for_type(self.type)
                if macro_name:
                    return macro_name

            # Fallback: 기존 하드코딩된 방식
            if 'utexture' in type_lower:
                return 'ADD_PROPERTY_TEXTURE'
            elif 'ustaticmesh' in type_lower:
                return 'ADD_PROPERTY_STATICMESH'
            elif 'uskeletalmesh' in type_lower:
                return 'ADD_PROPERTY_SKELETALMESH'
            elif 'uparticlesystem' in type_lower:
                return 'ADD_PROPERTY_PARTICLESYSTEM'
            elif 'umaterial' in type_lower:
                return 'ADD_PROPERTY_MATERIAL'
            elif 'usound' in type_lower:
                return 'ADD_PROPERTY_AUDIO'
            else:
                return 'ADD_PROPERTY'

        # 범위가 있는 프로퍼티
        if self.has_range:
            return 'ADD_PROPERTY_RANGE'

        return 'ADD_PROPERTY'


@dataclass
class Parameter:
    """함수 파라미터 정보"""
    name: str
    type: str


@dataclass
class Function:
    """함수 정보"""
    name: str
    display_name: str
    return_type: str
    parameters: List[Parameter] = field(default_factory=list)
    is_const: bool = False
    metadata: Dict[str, str] = field(default_factory=dict)

    def get_parameter_types_string(self) -> str:
        """템플릿 파라미터 문자열 생성: <UClass, uint32, const FString&, ...>"""
        if not self.parameters:
            return ""
        param_types = ", ".join([p.type for p in self.parameters])
        return f", {param_types}"


@dataclass
class ClassInfo:
    """클래스 정보"""
    name: str
    parent: str
    header_file: Path
    properties: List[Property] = field(default_factory=list)
    functions: List[Function] = field(default_factory=list)
    is_component: bool = False
    is_spawnable: bool = False
    display_name: str = ""
    description: str = ""
    uclass_metadata: Dict[str, str] = field(default_factory=dict)


class HeaderParser:
    """C++ 헤더 파일 파서"""

    # UPROPERTY 시작 패턴 (괄호는 별도 파싱)
    UPROPERTY_START = re.compile(r'UPROPERTY\s*\(')

    def __init__(self, project_root: Optional[Path] = None):
        """
        HeaderParser 초기화

        Args:
            project_root: 프로젝트 루트 경로 (MacroParser 초기화용)
                         None이면 현재 파일 기준으로 자동 감지
        """
        if project_root is None:
            # 현재 파일(header_parser.py)의 상위 2단계가 프로젝트 루트
            project_root = Path(__file__).parent.parent.parent

        # MacroParser 생성 및 파싱
        self.macro_parser = create_macro_parser(project_root)

        # Property 클래스에 MacroParser 설정 (전역)
        Property.set_macro_parser(self.macro_parser)

        print(f"[HeaderParser] Initialized with {len(self.macro_parser.macros)} macros from ObjectMacros.h")

    # UCLASS 시작 패턴
    UCLASS_START = re.compile(r'UCLASS\s*\(')

    # 기존 패턴들
    UFUNCTION_PATTERN = re.compile(
        r'UFUNCTION\s*\((.*?)\)\s*'
        r'(.*?)\s+(\w+)\s*\((.*?)\)\s*(const)?\s*[;{]',
        re.DOTALL
    )

    CLASS_PATTERN = re.compile(
        r'class\s+(\w+)\s*:\s*public\s+(\w+)'
    )

    GENERATED_REFLECTION_PATTERN = re.compile(
        r'GENERATED_REFLECTION_BODY\(\)'
    )

    @staticmethod
    def _extract_balanced_parens(text: str, start_pos: int) -> Tuple[str, int]:
        """괄호 매칭하여 내용 추출. Returns (content, end_position)"""
        depth = 1
        i = start_pos
        while i < len(text) and depth > 0:
            if text[i] == '(':
                depth += 1
            elif text[i] == ')':
                depth -= 1
            i += 1
        return text[start_pos:i-1], i

    @staticmethod
    def _parse_uproperty_declarations(content: str):
        """UPROPERTY 선언 찾기 (괄호 매칭 지원, 주석 제외)"""
        results = []
        pos = 0

        while True:
            match = HeaderParser.UPROPERTY_START.search(content, pos)
            if not match:
                break

            # 주석 처리된 UPROPERTY인지 확인
            line_start = content.rfind('\n', 0, match.start()) + 1
            line_before_uproperty = content[line_start:match.start()]

            # // 주석이나 /* */ 주석 안에 있는지 확인
            if '//' in line_before_uproperty:
                # 같은 줄에 // 주석이 있으면 건너뛰기
                pos = match.end()
                continue

            # 여러 줄 주석 /* */ 체크
            last_comment_start = content.rfind('/*', 0, match.start())
            last_comment_end = content.rfind('*/', 0, match.start())
            if last_comment_start > last_comment_end:
                # /* 안에 있으면 건너뛰기
                pos = match.end()
                continue

            # 괄호 안 메타데이터 추출
            metadata_start = match.end()
            metadata, metadata_end = HeaderParser._extract_balanced_parens(content, metadata_start)

            # 메타데이터 다음에 타입과 변수명 찾기
            remaining = content[metadata_end:metadata_end+200]  # 적당한 범위만
            # 타입과 변수명을 매칭 (초기화 부분은 무시: =...; 또는 {...}; 또는 ;)
            var_match = re.match(r'\s*([\w<>*:,\s]+?)\s+(\w+)\s*(?:[;=]|{[^}]*};?)', remaining)

            if var_match:
                var_type = var_match.group(1).strip()
                var_name = var_match.group(2)
                results.append((metadata, var_type, var_name))

            pos = metadata_end

        return results

    @staticmethod
    def _remove_comments(content: str) -> str:
        """C++ 주석 제거 (// 및 /* */ 주석)"""
        # 여러 줄 주석 /* */ 제거
        content = re.sub(r'/\*.*?\*/', '', content, flags=re.DOTALL)
        # 한 줄 주석 // 제거
        content = re.sub(r'//.*?$', '', content, flags=re.MULTILINE)
        return content

    def parse_header(self, header_path: Path) -> Optional[ClassInfo]:
        """헤더 파일 파싱"""
        content = header_path.read_text(encoding='utf-8')

        # 주석을 제거한 버전으로 GENERATED_REFLECTION_BODY() 체크
        content_no_comments = self._remove_comments(content)

        # GENERATED_REFLECTION_BODY() 체크
        if not self.GENERATED_REFLECTION_PATTERN.search(content_no_comments):
            return None

        # 클래스 정보 추출 (주석 제거된 버전에서)
        class_match = self.CLASS_PATTERN.search(content_no_comments)
        if not class_match:
            return None

        class_name = class_match.group(1)
        parent_name = class_match.group(2)

        class_info = ClassInfo(
            name=class_name,
            parent=parent_name,
            header_file=header_path
        )

        # UCLASS 메타데이터 파싱 (주석 제거된 버전에서)
        uclass_match = self.UCLASS_START.search(content_no_comments)
        if uclass_match:
            metadata_start = uclass_match.end()
            metadata, _ = self._extract_balanced_parens(content_no_comments, metadata_start)
            class_info.uclass_metadata = self._parse_metadata(metadata)

            # DisplayName과 Description 추출
            if 'DisplayName' in class_info.uclass_metadata:
                class_info.display_name = class_info.uclass_metadata['DisplayName']
            if 'Description' in class_info.uclass_metadata:
                class_info.description = class_info.uclass_metadata['Description']

        # UPROPERTY 파싱 (주석 제거된 버전에서)
        uproperty_decls = self._parse_uproperty_declarations(content_no_comments)
        for metadata_str, prop_type, prop_name in uproperty_decls:
            prop = self._parse_property(prop_name, prop_type, metadata_str)
            class_info.properties.append(prop)

        # UFUNCTION 파싱 (주석 제거된 버전에서)
        for match in self.UFUNCTION_PATTERN.finditer(content_no_comments):
            metadata_str = match.group(1)
            return_type = match.group(2).strip()
            func_name = match.group(3)
            params_str = match.group(4)
            is_const = match.group(5) is not None

            func = self._parse_function(
                func_name, return_type, params_str,
                metadata_str, is_const
            )
            class_info.functions.append(func)

        # AActor 클래스에 기본 프로퍼티 추가
        if class_name == 'AActor':
            object_name_prop = Property(
                name='ObjectName',
                type='FName',
                category='[액터]',
                editable=True,
                tooltip='액터의 이름입니다'
            )
            # 맨 앞에 추가
            class_info.properties.insert(0, object_name_prop)

        # UActorComponent 클래스에 기본 프로퍼티 추가
        elif class_name == 'UActorComponent':
            object_name_prop = Property(
                name='ObjectName',
                type='FName',
                category='[컴포넌트]',
                editable=True,
                tooltip='컴포넌트의 이름입니다'
            )
            # 맨 앞에 추가
            class_info.properties.insert(0, object_name_prop)

        return class_info

    def _parse_metadata(self, metadata: str) -> Dict[str, str]:
        """메타데이터 문자열을 파싱하여 딕셔너리로 반환"""
        result = {}

        # Key="Value" 패턴 찾기
        for match in re.finditer(r'(\w+)\s*=\s*"([^"]+)"', metadata):
            key = match.group(1)
            value = match.group(2)
            result[key] = value

        return result

    def _parse_property(self, name: str, type_str: str, metadata: str) -> Property:
        """프로퍼티 메타데이터 파싱"""
        prop = Property(name=name, type=type_str)

        # Category 추출
        category_match = re.search(r'Category\s*=\s*"([^"]+)"', metadata)
        if category_match:
            prop.category = category_match.group(1)

        # EditAnywhere 체크
        prop.editable = 'EditAnywhere' in metadata

        # Range 추출
        range_match = re.search(r'Range\s*=\s*"([^"]+)"', metadata)
        if range_match:
            range_str = range_match.group(1)
            min_max = range_str.split(',')
            if len(min_max) == 2:
                prop.has_range = True
                prop.min_value = float(min_max[0].strip())
                prop.max_value = float(min_max[1].strip())

        # Tooltip 추출
        tooltip_match = re.search(r'Tooltip\s*=\s*"([^"]+)"', metadata)
        if tooltip_match:
            prop.tooltip = tooltip_match.group(1)

        return prop

    def _parse_function(
        self, name: str, return_type: str,
        params_str: str, metadata: str, is_const: bool
    ) -> Function:
        """함수 메타데이터 파싱"""
        func = Function(
            name=name,
            display_name=name,
            return_type=return_type,
            is_const=is_const
        )

        # DisplayName 추출
        display_match = re.search(r'DisplayName\s*=\s*"([^"]+)"', metadata)
        if display_match:
            func.display_name = display_match.group(1)

        # LuaBind 체크
        func.metadata['lua_bind'] = 'LuaBind' in metadata

        # 파라미터 파싱
        if params_str.strip():
            params = [p.strip() for p in params_str.split(',')]
            for param in params:
                # "const FString& Name" -> type="const FString&", name="Name"
                parts = param.rsplit(None, 1)
                if len(parts) == 2:
                    param_type = parts[0]
                    param_name = parts[1]
                    func.parameters.append(Parameter(name=param_name, type=param_type))

        return func

    def find_reflection_classes(self, source_dir: Path) -> List[ClassInfo]:
        """소스 디렉토리에서 GENERATED_REFLECTION_BODY가 있는 모든 클래스 찾기"""
        classes = []

        for header in source_dir.rglob("*.h"):
            try:
                class_info = self.parse_header(header)
                if class_info:
                    classes.append(class_info)
                    print(f"[OK] Found reflection class: {class_info.name} in {header.name}")
            except Exception as e:
                print(f" Error parsing {header}: {e}")

        return classes
