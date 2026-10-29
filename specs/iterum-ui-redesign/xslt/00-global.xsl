<?xml version="1.0" encoding="UTF-8"?>
<!--
  00-global.xsl - the global part of specs/iterum-ui-redesign/design-system.md (sections 2, 4.3, 5, 9.2 step 1).

  Identity transform plus:
    - <colors>, <fonts>, <gradients> replaced with sections 5.2, 5.4, 5.3;
    - template "Editor" replaced with section 2.2 (fixed 940 x 608, header + mode strip, the 900 x 480 panel frame,
      footer with the Gain knob / label / readout and the shared views, the UpdateBanner as the last view);
    - every CSlider, anywhere in the file: back-color / value-color / frame-color (which CSlider never reads) become
      draw-back-color / draw-value-color / draw-frame-color with the section 4.3 values;
    - the ten panel title labels (CTextLabel font="section-font" directly under a panel template) removed, and any
      other use of the removed fonts section-font / mode-font becomes label-font;
    - any attribute naming a removed colour (sidebar, section, segment-normal, segment-selected) is mapped to its
      5.2 replacement so no view references a colour that no longer exists.
  Everything else, including the ten panel templates' layout, is copied unchanged: the per-panel stylesheets
  rebuild those.

  Apply:  npx xslt3 -xsl:specs/iterum-ui-redesign/xslt/00-global.xsl -s:plugins/iterum/resources/editor.uidesc -o:OUT
-->
<xsl:stylesheet version="3.0"
                xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
                xmlns:xs="http://www.w3.org/2001/XMLSchema"
                xmlns:local="urn:iterum-ui"
                xmlns:saxon="http://saxon.sf.net/"
                exclude-result-prefixes="xs local saxon">

  <!-- Two-space indent, as the file has always been written. -->
  <xsl:output method="xml" encoding="UTF-8" indent="yes" saxon:indent-spaces="2" saxon:line-length="100000"/>
  <xsl:strip-space elements="*"/>
  <xsl:mode on-no-match="shallow-copy"/>

  <xsl:variable name="panel-templates" as="xs:string+"
                select="('GranularPanel', 'SpectralPanel', 'ShimmerPanel', 'TapePanel', 'BBDPanel', 'DigitalPanel',
                         'PingPongPanel', 'ReversePanel', 'MultiTapPanel', 'FreezePanel')"/>

  <!-- Removed colour tokens and what replaces them (5.2: "unused or replaced"). -->
  <xsl:function name="local:replacement-color" as="xs:string?">
    <xsl:param name="name" as="xs:string"/>
    <xsl:sequence select="map{'sidebar': 'slider-track', 'section': 'panel',
                              'segment-normal': 'slider-track', 'segment-selected': 'accent-dim'}($name)"/>
  </xsl:function>

  <!-- ===================================================================== 5.2 colour tokens -->
  <xsl:template match="/vstgui-ui-description/colors">
    <colors>
      <color name="background" rgba="#121216ff"/>
      <color name="header" rgba="#1e1e26ff"/>
      <color name="panel" rgba="#1b1b22ff"/>
      <color name="border" rgba="#30303aff"/>
      <color name="slider-track" rgba="#272730ff"/>
      <color name="slider-frame" rgba="#3a3a46ff"/>
      <color name="accent" rgba="#5aa0e6ff"/>
      <color name="accent-dim" rgba="#2e5a88ff"/>
      <color name="text" rgba="#e6e6ecff"/>
      <color name="text-dim" rgba="#9c9caaff"/>
      <color name="text-light" rgba="#ffffffff"/>
    </colors>
  </xsl:template>

  <!-- ===================================================================== 5.4 fonts -->
  <xsl:template match="/vstgui-ui-description/fonts">
    <fonts>
      <font name="title-font" font-name="Arial" size="20" bold="true"/>
      <font name="readout-font" font-name="Arial" size="15" bold="true"/>
      <font name="label-font" font-name="Arial" size="11"/>
      <font name="value-font" font-name="Arial" size="11"/>
    </fonts>
  </xsl:template>

  <!-- ===================================================================== 5.3 gradients -->
  <xsl:template match="/vstgui-ui-description/gradients">
    <gradients>
      <gradient name="segment-bg">
        <color-stop rgba="#2a2a33ff" start="0"/>
        <color-stop rgba="#24242cff" start="1"/>
      </gradient>
      <gradient name="segment-bg-selected">
        <color-stop rgba="#3568a0ff" start="0"/>
        <color-stop rgba="#2e5a88ff" start="1"/>
      </gradient>
    </gradients>
  </xsl:template>

  <!-- ===================================================================== 2.1 / 2.2 the Editor template -->
  <xsl:template match="template[@name = 'Editor']">
    <!-- The panel switch keeps the existing template list and switch control. -->
    <xsl:variable name="switch" select=".//view[@class = 'UIViewSwitchContainer']"/>
    <xsl:variable name="mode" select=".//view[@class = 'IconSegmentButton'][@control-tag = 'Mode']"/>
    <template name="Editor" class="CViewContainer" origin="0, 0" size="940, 608" background-color="background"
              minSize="940, 608" maxSize="940, 608">
      <!-- Header: title + mode strip (right edge 920 = panel right edge). -->
      <view class="CViewContainer" origin="0, 0" size="940, 52" background-color="header" mouse-enabled="true">
        <view class="CTextLabel" origin="20, 13" size="100, 26" title="ITERUM" font="title-font" font-color="accent"
              text-alignment="left" transparent="true"/>
        <view class="IconSegmentButton" origin="130, 6" size="790, 40" control-tag="Mode" layout-mode="icon-and-text"
              segment-names="{$mode/@segment-names}" segment-icons="{$mode/@segment-icons}"
              highlight-color="accent-dim" selected-color="text-light" unselected-color="text-dim"
              frame-color="border" round-radius="4" font-size="11" font-name="Arial"/>
      </view>
      <!-- Mode panel frame: every panel template is 900 x 480. -->
      <view class="UIViewSwitchContainer" origin="20, 64" size="900, 480" background-color="panel"
            transparent="false" animation-time="0" template-names="{$switch/@template-names}"
            template-switch-control="{$switch/@template-switch-control}"/>
      <!-- Footer: Gain left, version / updates / presets right (buttons end at x = 920). -->
      <view class="CViewContainer" origin="0, 556" size="940, 52" background-color="header">
        <view class="ArcKnob" origin="20, 10" size="32, 32" control-tag="Gain" arc-color="accent"
              guide-color="slider-frame" arc-width="2.5" popup-position="top"/>
        <view class="CTextLabel" origin="60, 10" size="80, 16" title="Gain" font="label-font" font-color="text-dim"
              text-alignment="left" transparent="true"/>
        <view class="CParamDisplay" origin="60, 28" size="80, 16" control-tag="Gain" font="value-font"
              font-color="text" text-alignment="left" back-color="~ TransparentCColor"
              frame-color="~ TransparentCColor" style-no-frame="true"/>
        <view class="CView" custom-view-name="VersionLabel" origin="430, 18" size="110, 16" mouse-enabled="false"/>
        <view custom-view-name="CheckForUpdatesButton" origin="560, 14" size="120, 24"/>
        <view custom-view-name="SavePresetButton" origin="690, 14" size="110, 24"/>
        <view custom-view-name="PresetBrowserButton" origin="810, 14" size="110, 24"/>
      </view>
      <!-- Update banner: the last view, exactly the header (it never touches the panel). -->
      <view custom-view-name="UpdateBanner" origin="0, 0" size="940, 52"/>
    </template>
  </xsl:template>

  <!-- ===================================================================== 4.3 the CSlider colour attributes -->
  <xsl:template match="view[@class = 'CSlider']/@back-color">
    <xsl:attribute name="draw-back-color" select="'slider-track'"/>
  </xsl:template>
  <xsl:template match="view[@class = 'CSlider']/@value-color">
    <xsl:attribute name="draw-value-color" select="'accent'"/>
  </xsl:template>
  <xsl:template match="view[@class = 'CSlider']/@frame-color">
    <xsl:attribute name="draw-frame-color" select="'slider-frame'"/>
  </xsl:template>
  <!-- A slider that already carries a draw-*-color (none today) takes the 4.3 value too. -->
  <xsl:template match="view[@class = 'CSlider']/@draw-back-color" priority="2">
    <xsl:attribute name="draw-back-color" select="'slider-track'"/>
  </xsl:template>
  <xsl:template match="view[@class = 'CSlider']/@draw-value-color" priority="2">
    <xsl:attribute name="draw-value-color" select="'accent'"/>
  </xsl:template>
  <xsl:template match="view[@class = 'CSlider']/@draw-frame-color" priority="2">
    <xsl:attribute name="draw-frame-color" select="'slider-frame'"/>
  </xsl:template>

  <!-- ===================================================================== 9.2 step 1: panel titles and removed fonts -->
  <xsl:template match="template[@name = $panel-templates]/view[@class = 'CTextLabel'][@font = 'section-font']"/>

  <xsl:template match="@font[. = ('section-font', 'mode-font')]">
    <xsl:attribute name="font" select="'label-font'"/>
  </xsl:template>

  <!-- Removed colour names in any *color attribute (outside the CSlider renames above). -->
  <xsl:template match="view/@*[ends-with(local-name(), 'color')][. = ('sidebar', 'section', 'segment-normal', 'segment-selected')]">
    <xsl:attribute name="{name()}" select="local:replacement-color(string(.))"/>
  </xsl:template>

</xsl:stylesheet>
